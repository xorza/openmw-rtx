#pragma once

#include <QObject>
#include <filesystem>

#include <components/toutf8/toutf8.hpp>

#include "operation.hpp"
#include "savingstate.hpp"

namespace CSMDoc
{
    class Document;

    class Saving : public Operation
    {
        Q_OBJECT

        Document& mDocument;
        SavingState mState;

    public:
        Saving(Document& document, const std::filesystem::path& projectPath, ToUTF8::FromType encoding);
    };
}
