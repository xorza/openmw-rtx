#pragma once

#include <cstdint>
#include <string>

#include <QTableWidget>

#include <apps/opencs/model/doc/messages.hpp>
#include <apps/opencs/model/world/scriptcontext.hpp>
#include <components/compiler/errorhandler.hpp>
#include <components/compiler/extensions.hpp>

namespace Compiler
{
    struct TokenLoc;
}

namespace CSMDoc
{
    class Document;
}

namespace CSMPrefs
{
    class Setting;
}

namespace CSVWorld
{
    class ScriptErrorTable : public QTableWidget, private Compiler::ErrorHandler
    {
        Q_OBJECT

        Compiler::Extensions mExtensions;
        CSMWorld::ScriptContext mContext;

        void report(const std::string& message, const Compiler::TokenLoc& loc, Type type) override;
        ///< Report error to the user.

        void report(const std::string& message, Type type) override;
        ///< Report a file related error

        void addMessage(const std::string& message, CSMDoc::Message::Severity severity, std::int32_t line = -1,
            std::int32_t column = -1);

        void setWarningsMode(const std::string& value);

    public:
        explicit ScriptErrorTable(const CSMDoc::Document& document, QWidget* parent = nullptr);

        void update(const std::string& source);

        // Hides the base's member on purpose: it is called through the derived type.
        // NOLINTNEXTLINE(bugprone-derived-method-shadowing-base-method)
        void clear();

        /// Clear local variable cache for \a script.
        ///
        /// \return Were there any locals that needed clearing?
        bool clearLocals(const std::string& script);

    private slots:

        void settingChanged(const CSMPrefs::Setting* setting);

        void cellClicked(std::int32_t row, std::int32_t column);

    signals:

        void highlightError(std::int32_t line, std::int32_t column);
    };
}
