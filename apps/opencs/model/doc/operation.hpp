#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include <QObject>

#include "messages.hpp"
#include "state.hpp"

namespace CSMDoc
{
    class Stage;

    class Operation : public QObject
    {
        Q_OBJECT

        State mType;
        std::vector<std::pair<Stage*, std::int32_t>> mStages; // stage, number of steps
        std::vector<std::pair<Stage*, std::int32_t>>::iterator mCurrentStage;
        std::int32_t mCurrentStep;
        std::int32_t mCurrentStepTotal;
        std::int32_t mTotalSteps;
        bool mFinalAlways;
        bool mError;
        bool mPrepared;
        Message::Severity mDefaultSeverity;
        std::optional<std::chrono::steady_clock::time_point> mStart;

        void prepareStages();

    public:
        explicit Operation(State type, bool finalAlways = false);
        /// \param finalAlways Execute last stage even if an error occurred during earlier stages.

        ~Operation() override;

        void appendStage(Stage* stage);
        ///< The ownership of \a stage is transferred to *this.
        ///
        /// \attention Do no call this function while this Operation is running.

        /// \attention Do no call this function while this Operation is running.
        void setDefaultSeverity(Message::Severity severity);

        bool hasError() const;

    signals:

        void progress(std::int32_t current, std::int32_t max, std::int32_t type);

        void reportMessage(const CSMDoc::Message& message, std::int32_t type);

        void done(std::int32_t type, bool failed);

    public slots:

        void abort();

        void run();

        /// Stop the timer and move this operation back to the main thread.
        /// Called via DirectConnection from QThread::finished so it executes
        /// on the worker thread before it fully exits.
        void cleanup();

    private slots:

        void executeStage();

    protected slots:

        virtual void operationDone();
    };
}
