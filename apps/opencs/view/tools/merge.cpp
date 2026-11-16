
#include "merge.hpp"

#include <QDialogButtonBox>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSplitter>
#include <QVBoxLayout>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/doc/documentmanager.hpp>
#include <apps/opencs/model/doc/state.hpp>
#include <apps/opencs/view/doc/adjusterwidget.hpp>
#include <apps/opencs/view/doc/filewidget.hpp>
#include <components/files/qtconversion.hpp>

void CSVTools::Merge::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape)
    {
        event->accept();
        cancel();
    }
    else
        QWidget::keyPressEvent(event);
}

CSVTools::Merge::Merge(CSMDoc::DocumentManager& documentManager, QWidget* parent)
    : QWidget(parent)
    , mDocument(nullptr)
    , mDocumentManager(documentManager)
{
    setWindowTitle("Merge Content Files into a new Game File");

    auto* mainLayout = new QVBoxLayout;
    setLayout(mainLayout);

    auto* splitter = new QSplitter(Qt::Horizontal, this);

    mainLayout->addWidget(splitter, 1);

    // left panel (files to be merged)
    auto* left = new QWidget(this);
    left->setContentsMargins(0, 0, 0, 0);
    splitter->addWidget(left);

    auto* leftLayout = new QVBoxLayout;
    left->setLayout(leftLayout);

    leftLayout->addWidget(new QLabel("Files to be merged", this));

    mFiles = new QListWidget(this);
    leftLayout->addWidget(mFiles, 1);

    // right panel (new game file)
    auto* right = new QWidget(this);
    right->setContentsMargins(0, 0, 0, 0);
    splitter->addWidget(right);

    auto* rightLayout = new QVBoxLayout;
    rightLayout->setAlignment(Qt::AlignTop);
    right->setLayout(rightLayout);

    rightLayout->addWidget(new QLabel("New game file", this));

    mNewFile = new CSVDoc::FileWidget(this);
    mNewFile->setType(false);
    mNewFile->extensionLabelIsVisible(true);
    rightLayout->addWidget(mNewFile);

    mAdjuster = new CSVDoc::AdjusterWidget(this);

    rightLayout->addWidget(mAdjuster);

    connect(mNewFile, &CSVDoc::FileWidget::nameChanged, mAdjuster, &CSVDoc::AdjusterWidget::setName);
    connect(mAdjuster, &CSVDoc::AdjusterWidget::stateChanged, this, &Merge::stateChanged);

    // buttons
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, Qt::Horizontal, this);

    connect(buttons->button(QDialogButtonBox::Cancel), &QPushButton::clicked, this, &Merge::cancel);

    mOkay = new QPushButton("Merge", this);
    connect(mOkay, &QPushButton::clicked, this, &Merge::accept);
    mOkay->setDefault(true);
    buttons->addButton(mOkay, QDialogButtonBox::AcceptRole);

    mainLayout->addWidget(buttons);
}

void CSVTools::Merge::configure(CSMDoc::Document* document)
{
    mDocument = document;

    mNewFile->setName("");

    // content files
    while (mFiles->count())
        delete mFiles->takeItem(0);

    std::vector<std::filesystem::path> files = document->getContentFiles();

    for (const auto& file : files)
        mFiles->addItem(Files::pathToQString(file.filename()));
}

void CSVTools::Merge::setLocalData(const std::filesystem::path& localData)
{
    mAdjuster->setLocalData(localData);
}

CSMDoc::Document* CSVTools::Merge::getDocument() const
{
    return mDocument;
}

void CSVTools::Merge::cancel()
{
    mDocument = nullptr;
    hide();
}

void CSVTools::Merge::accept()
{
    if ((mDocument->getState() & CSMDoc::State_Merging) == 0)
    {
        std::vector<std::filesystem::path> files{ mAdjuster->getPath() };

        std::unique_ptr<CSMDoc::Document> target(mDocumentManager.makeDocument(files, files[0], true));

        mDocument->runMerge(std::move(target));

        hide();
    }
}

void CSVTools::Merge::stateChanged(bool valid)
{
    mOkay->setEnabled(valid);
}
