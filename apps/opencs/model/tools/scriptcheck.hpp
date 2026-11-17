#pragma once

#include <cstdint>
#include <string>

#include <apps/opencs/model/doc/messages.hpp>
#include <apps/opencs/model/doc/stage.hpp>
#include <apps/opencs/model/world/scriptcontext.hpp>
#include <components/compiler/errorhandler.hpp>
#include <components/compiler/extensions.hpp>

namespace CSMDoc
{
    class Document;
}

namespace Compiler
{
    struct TokenLoc;
}

namespace CSMTools
{
    /// \brief VerifyStage: make sure that scripts compile
    class ScriptCheckStage : public CSMDoc::Stage, private Compiler::ErrorHandler
    {
        enum WarningMode
        {
            Mode_Ignore,
            Mode_Normal,
            Mode_Strict
        };

        const CSMDoc::Document& mDocument;
        Compiler::Extensions mExtensions;
        CSMWorld::ScriptContext mContext;
        ESM::RefId mId;
        std::string mFile;
        CSMDoc::Messages* mMessages;
        WarningMode mWarningMode;
        bool mIgnoreBaseRecords;

        CSMDoc::Message::Severity getSeverity(Type type);

        void report(const std::string& message, const Compiler::TokenLoc& loc, Type type) override;
        ///< Report error to the user.

        void report(const std::string& message, Type type) override;
        ///< Report a file related error

    public:
        explicit ScriptCheckStage(const CSMDoc::Document& document);

        std::int32_t setup() override;
        ///< \return number of steps

        void perform(std::int32_t stage, CSMDoc::Messages& messages) override;
        ///< Messages resulting from this tage will be appended to \a messages.
    };
}
