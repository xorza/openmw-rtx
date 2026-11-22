#include "scenetooltexturebrush.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <QButtonGroup>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMargins>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <apps/opencs/model/doc/document.hpp>
#include <apps/opencs/model/prefs/category.hpp>
#include <apps/opencs/model/prefs/setting.hpp>
#include <apps/opencs/model/prefs/state.hpp>
#include <apps/opencs/model/world/columns.hpp>
#include <apps/opencs/model/world/commandmacro.hpp>
#include <apps/opencs/model/world/commands.hpp>
#include <apps/opencs/model/world/data.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/idtable.hpp>
#include <apps/opencs/model/world/record.hpp>
#include <apps/opencs/model/world/universalid.hpp>
#include <components/misc/scalableicon.hpp>

#include "brushshapes.hpp"
#include "pushbutton.hpp"
#include "scenetool.hpp"
#include "scenetoollistpanel.hpp"

namespace CSVWidget
{
    class SceneToolbar;
}

CSVWidget::BrushSizeControls::BrushSizeControls(const QString& title, QWidget* parent)
    : QGroupBox(title, parent)
    , mLayoutSliderSize(new QHBoxLayout)
    , mBrushSizeSlider(new QSlider(Qt::Horizontal))
    , mBrushSizeSpinBox(new QSpinBox)
{
    mBrushSizeSlider->setTickPosition(QSlider::TicksBothSides);
    mBrushSizeSlider->setTickInterval(10);
    mBrushSizeSlider->setRange(1, CSMPrefs::get()["3D Scene Editing"]["texturebrush-maximumsize"].toInt());
    mBrushSizeSlider->setSingleStep(1);

    mBrushSizeSpinBox->setRange(1, CSMPrefs::get()["3D Scene Editing"]["texturebrush-maximumsize"].toInt());
    mBrushSizeSpinBox->setSingleStep(1);

    mLayoutSliderSize->addWidget(mBrushSizeSlider);
    mLayoutSliderSize->addWidget(mBrushSizeSpinBox);

    connect(mBrushSizeSlider, &QSlider::valueChanged, mBrushSizeSpinBox, &QSpinBox::setValue);
    connect(mBrushSizeSpinBox, qOverload<std::int32_t>(&QSpinBox::valueChanged), mBrushSizeSlider, &QSlider::setValue);

    setLayout(mLayoutSliderSize);
}

CSVWidget::TextureBrushWindow::TextureBrushWindow(CSMDoc::Document& document, QWidget* parent)
    : QFrame(parent, Qt::Popup)
    , mDocument(document)
{
    mBrushTextureLabel = "Selected texture: " + mBrushTexture.getRefIdString() + " ";

    CSMWorld::IdCollection<ESM::LandTexture>& landtexturesCollection = mDocument.getData().getLandTextures();

    std::int32_t landTextureFilename = landtexturesCollection.findColumnIndex(CSMWorld::Columns::ColumnId_Texture);
    const std::int32_t index = landtexturesCollection.searchId(mBrushTexture);

    if (index != -1 && !landtexturesCollection.getRecord(index).isDeleted())
    {
        mSelectedBrush = new QLabel(QString::fromStdString(mBrushTextureLabel)
            + landtexturesCollection.getData(index, landTextureFilename).value<QString>());
        mSelectedBrush->setTextFormat(Qt::PlainText);
    }
    else
    {
        mBrushTextureLabel = "No selected texture or invalid texture";
        mSelectedBrush = new QLabel(QString::fromStdString(mBrushTextureLabel));
        mSelectedBrush->setTextFormat(Qt::PlainText);
    }

    mButtonPoint = new QPushButton(Misc::ScalableIcon::load(":scenetoolbar/brush-point"), "", this);
    mButtonSquare = new QPushButton(Misc::ScalableIcon::load(":scenetoolbar/brush-square"), "", this);
    mButtonCircle = new QPushButton(Misc::ScalableIcon::load(":scenetoolbar/brush-circle"), "", this);
    mButtonCustom = new QPushButton(Misc::ScalableIcon::load(":scenetoolbar/brush-custom"), "", this);

    mSizeSliders = new BrushSizeControls("Brush size", this);

    auto* layoutMain = new QVBoxLayout;
    layoutMain->setSpacing(0);
    layoutMain->setContentsMargins(4, 0, 4, 4);

    auto* layoutHorizontal = new QHBoxLayout;
    layoutHorizontal->setSpacing(0);
    layoutHorizontal->setContentsMargins(QMargins(0, 0, 0, 0));

    configureButtonInitialSettings(mButtonPoint);
    configureButtonInitialSettings(mButtonSquare);
    configureButtonInitialSettings(mButtonCircle);
    configureButtonInitialSettings(mButtonCustom);

    mButtonPoint->setToolTip(toolTipPoint);
    mButtonSquare->setToolTip(toolTipSquare);
    mButtonCircle->setToolTip(toolTipCircle);
    mButtonCustom->setToolTip(toolTipCustom);

    auto* brushButtonGroup = new QButtonGroup(this);
    brushButtonGroup->addButton(mButtonPoint);
    brushButtonGroup->addButton(mButtonSquare);
    brushButtonGroup->addButton(mButtonCircle);
    brushButtonGroup->addButton(mButtonCustom);

    brushButtonGroup->setExclusive(true);

    layoutHorizontal->addWidget(mButtonPoint, 0, Qt::AlignTop);
    layoutHorizontal->addWidget(mButtonSquare, 0, Qt::AlignTop);
    layoutHorizontal->addWidget(mButtonCircle, 0, Qt::AlignTop);
    layoutHorizontal->addWidget(mButtonCustom, 0, Qt::AlignTop);

    mHorizontalGroupBox = new QGroupBox(tr(""));
    mHorizontalGroupBox->setLayout(layoutHorizontal);

    layoutMain->addWidget(mHorizontalGroupBox);
    layoutMain->addWidget(mSizeSliders);
    layoutMain->addWidget(mSelectedBrush);

    setLayout(layoutMain);

    connect(mButtonPoint, &QPushButton::clicked, this, &TextureBrushWindow::setBrushShape);
    connect(mButtonSquare, &QPushButton::clicked, this, &TextureBrushWindow::setBrushShape);
    connect(mButtonCircle, &QPushButton::clicked, this, &TextureBrushWindow::setBrushShape);
    connect(mButtonCustom, &QPushButton::clicked, this, &TextureBrushWindow::setBrushShape);
}

void CSVWidget::TextureBrushWindow::configureButtonInitialSettings(QPushButton* button)
{
    button->setSizePolicy(QSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed));
    button->setContentsMargins(QMargins(0, 0, 0, 0));
    button->setIconSize(QSize(48 - 6, 48 - 6));
    button->setFixedSize(48, 48);
    button->setCheckable(true);
}

void CSVWidget::TextureBrushWindow::setBrushTexture(ESM::RefId brushTexture)
{
    CSMWorld::IdTable& ltexTable = dynamic_cast<CSMWorld::IdTable&>(
        *mDocument.getData().getTableModel(CSMWorld::UniversalId::Type_LandTextures));
    QUndoStack& undoStack = mDocument.getUndoStack();

    CSMWorld::IdCollection<ESM::LandTexture>& landtexturesCollection = mDocument.getData().getLandTextures();
    std::int32_t landTextureFilename = landtexturesCollection.findColumnIndex(CSMWorld::Columns::ColumnId_Texture);

    const std::int32_t row = landtexturesCollection.searchId(brushTexture);

    const CSMWorld::Record<ESM::LandTexture>* record = row != -1 ? &landtexturesCollection.getRecord(row) : nullptr;

    if (record != nullptr && !record->isDeleted())
    {
        // Ensure the texture is defined by the current plugin
        if (!record->isModified())
        {
            CSMWorld::CommandMacro macro(undoStack);
            macro.push(new CSMWorld::TouchCommand(ltexTable, brushTexture.getRefIdString()));
        }
        mBrushTextureLabel = "Selected texture: " + brushTexture.getRefIdString() + " ";
        mSelectedBrush->setText(QString::fromStdString(mBrushTextureLabel)
            + landtexturesCollection.getData(row, landTextureFilename).value<QString>());
    }
    else
    {
        brushTexture = {};
        mBrushTextureLabel = "No selected texture or invalid texture";
        mSelectedBrush->setText(QString::fromStdString(mBrushTextureLabel));
    }

    mBrushTexture = brushTexture;

    emit passTextureId(mBrushTexture);
    emit passBrushShape(mBrushShape); // updates the icon tooltip
}

void CSVWidget::TextureBrushWindow::setBrushSize(std::int32_t brushSize)
{
    mBrushSize = brushSize;
    emit passBrushSize(mBrushSize);
}

void CSVWidget::TextureBrushWindow::setBrushShape()
{
    if (mButtonPoint->isChecked())
        mBrushShape = CSVWidget::BrushShape_Point;
    if (mButtonSquare->isChecked())
        mBrushShape = CSVWidget::BrushShape_Square;
    if (mButtonCircle->isChecked())
        mBrushShape = CSVWidget::BrushShape_Circle;
    if (mButtonCustom->isChecked())
        mBrushShape = CSVWidget::BrushShape_Custom;
    emit passBrushShape(mBrushShape);
}

void CSVWidget::SceneToolTextureBrush::adjustToolTips() {}

CSVWidget::SceneToolTextureBrush::SceneToolTextureBrush(
    SceneToolbar* parent, QString toolTip, CSMDoc::Document& document)
    : SceneTool(parent, Type_TopAction)
    , mToolTip(std::move(toolTip))
    , mDocument(document)
    , mPanel(new SceneToolListPanel(2, this))
    , mTextureBrushWindow(new TextureBrushWindow(document, this))
{
    setAcceptDrops(true);
    connect(mTextureBrushWindow, &TextureBrushWindow::passBrushShape, this, &SceneToolTextureBrush::setButtonIcon);
    setButtonIcon(mTextureBrushWindow->mBrushShape);

    connect(mPanel, &SceneToolListPanel::selectRequest, this, &SceneToolTextureBrush::selectRow);
    connect(mPanel, &SceneToolListPanel::removeRequest, this, &SceneToolTextureBrush::removeRow);
}

void CSVWidget::SceneToolTextureBrush::setButtonIcon(CSVWidget::BrushShape brushShape)
{
    QString tooltip = "Change brush settings <p>Currently selected: ";

    switch (brushShape)
    {
        case BrushShape_Point:

            setIcon(Misc::ScalableIcon::load(":scenetoolbar/brush-point"));
            tooltip += mTextureBrushWindow->toolTipPoint;
            break;

        case BrushShape_Square:

            setIcon(Misc::ScalableIcon::load(":scenetoolbar/brush-square"));
            tooltip += mTextureBrushWindow->toolTipSquare;
            break;

        case BrushShape_Circle:

            setIcon(Misc::ScalableIcon::load(":scenetoolbar/brush-circle"));
            tooltip += mTextureBrushWindow->toolTipCircle;
            break;

        case BrushShape_Custom:

            setIcon(Misc::ScalableIcon::load(":scenetoolbar/brush-custom"));
            tooltip += mTextureBrushWindow->toolTipCustom;
            break;
    }

    tooltip += "<p>(right click to access of previously used brush settings)";

    CSMWorld::IdCollection<ESM::LandTexture>& landtexturesCollection = mDocument.getData().getLandTextures();

    std::int32_t landTextureFilename = landtexturesCollection.findColumnIndex(CSMWorld::Columns::ColumnId_Texture);
    const std::int32_t index = landtexturesCollection.searchId(mTextureBrushWindow->mBrushTexture);

    if (index != -1 && !landtexturesCollection.getRecord(index).isDeleted())
    {
        tooltip += "<p>Selected texture: " + QString::fromStdString(mTextureBrushWindow->mBrushTexture.getRefIdString())
            + " ";

        tooltip += landtexturesCollection.getData(index, landTextureFilename).value<QString>();
    }
    else
    {
        tooltip += "<p>No selected texture or invalid texture";
    }

    tooltip += "<br>(drop texture here to change)";
    setToolTip(tooltip);
}

void CSVWidget::SceneToolTextureBrush::showPanel(const QPoint& position)
{
    updatePanel();
    mPanel->showAt(position);
}

void CSVWidget::SceneToolTextureBrush::updatePanel()
{
    CSMWorld::IdCollection<ESM::LandTexture>& landtexturesCollection = mDocument.getData().getLandTextures();
    const std::int32_t landTextureFilename
        = landtexturesCollection.findColumnIndex(CSMWorld::Columns::ColumnId_Texture);

    std::vector<QStringList> rows;
    rows.reserve(mBrushHistory.size());

    for (const ESM::RefId& brushTexture : mBrushHistory)
    {
        const std::int32_t index = landtexturesCollection.searchId(brushTexture);
        QString filename = "Invalid/deleted texture";

        if (index != -1 && !landtexturesCollection.getRecord(index).isDeleted())
            filename = landtexturesCollection.getData(index, landTextureFilename).value<QString>();

        rows.push_back(QStringList{ QString::fromStdString(brushTexture.getRefIdString()), filename });
    }

    mPanel->setRows(rows);
}

void CSVWidget::SceneToolTextureBrush::updateBrushHistory(ESM::RefId brushTexture)
{
    mBrushHistory.insert(mBrushHistory.begin(), brushTexture);
    if (mBrushHistory.size() > 5)
        mBrushHistory.pop_back();
}

void CSVWidget::SceneToolTextureBrush::selectRow(std::int32_t row)
{
    const ESM::RefId brushTexture = mBrushHistory[static_cast<std::size_t>(row)];
    std::swap(mBrushHistory[static_cast<std::size_t>(row)], mBrushHistory[0]);
    mTextureBrushWindow->setBrushTexture(brushTexture);
    emit passTextureId(mTextureBrushWindow->mBrushTexture);
}

void CSVWidget::SceneToolTextureBrush::removeRow(std::int32_t row)
{
    mBrushHistory.erase(mBrushHistory.begin() + row);
    updatePanel();
}

void CSVWidget::SceneToolTextureBrush::activate()
{
    QPoint position = QCursor::pos();
    mTextureBrushWindow->mSizeSliders->mBrushSizeSlider->setRange(
        1, CSMPrefs::get()["3D Scene Editing"]["texturebrush-maximumsize"].toInt());
    mTextureBrushWindow->mSizeSliders->mBrushSizeSpinBox->setRange(
        1, CSMPrefs::get()["3D Scene Editing"]["texturebrush-maximumsize"].toInt());
    mTextureBrushWindow->move(position);
    mTextureBrushWindow->show();
}

void CSVWidget::SceneToolTextureBrush::dragEnterEvent(QDragEnterEvent* event)
{
    emit passEvent(event);
    event->accept();
}
void CSVWidget::SceneToolTextureBrush::dropEvent(QDropEvent* event)
{
    emit passEvent(event);
    event->accept();
}
