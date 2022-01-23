#include "sceneprintpreviewdlg.h"

#include "utils/scene/basicgraphview.h"
#include "printing/helper/model/printpreviewsceneproxy.h"

#include <QVBoxLayout>
#include <QToolBar>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>

#include "utils/owningqpointer.h"
#include "printing/helper/view/custompagesetupdlg.h"
#include <QPageSetupDialog>
#include <QPrinter>

#include <QEvent>

ScenePrintPreviewDlg::ScenePrintPreviewDlg(QWidget *parent) :
    QDialog(parent),
    m_printer(nullptr)
{
    QVBoxLayout *lay = new QVBoxLayout(this);

    QToolBar *toolBar = new QToolBar;
    lay->addWidget(toolBar);

    zoomSlider = new QSlider(Qt::Horizontal);
    zoomSlider->setRange(25, 400);
    zoomSlider->setTickPosition(QSlider::TicksBelow);
    zoomSlider->setTickInterval(50);
    zoomSlider->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
    zoomSlider->setValue(100);
    zoomSlider->setToolTip(tr("Double click to reset zoom"));
    connect(zoomSlider, &QSlider::valueChanged, this,
            &ScenePrintPreviewDlg::updateZoomLevel);
    zoomSlider->installEventFilter(this);
    QAction *zoomAct = toolBar->addWidget(zoomSlider);
    zoomAct->setText(tr("Zoom:"));

    zoomSpinBox = new QSpinBox;
    zoomSpinBox->setRange(25, 400);
    zoomSpinBox->setValue(100);
    zoomSpinBox->setSuffix(QChar('%'));
    zoomSpinBox->setToolTip(tr("Zoom"));
    connect(zoomSpinBox, qOverload<int>(&QSpinBox::valueChanged),
            this, &ScenePrintPreviewDlg::updateZoomLevel);
    toolBar->addWidget(zoomSpinBox);

    toolBar->addSeparator();

    sceneScaleSpinBox = new QDoubleSpinBox;
    sceneScaleSpinBox->setRange(5, 200);
    sceneScaleSpinBox->setValue(100);
    sceneScaleSpinBox->setSuffix(QChar('%'));
    sceneScaleSpinBox->setToolTip(tr("Source scale factor"));
    connect(sceneScaleSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &ScenePrintPreviewDlg::onScaleChanged);
    QAction *scaleAct = toolBar->addWidget(sceneScaleSpinBox);
    scaleAct->setText(tr("Scale:"));

    marginSpinBox = new QDoubleSpinBox;
    marginSpinBox->setRange(0, 200);
    marginSpinBox->setValue(20);
    marginSpinBox->setSuffix(QLatin1String("px"));
    marginSpinBox->setToolTip(tr("Margins"));
    connect(marginSpinBox, qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, &ScenePrintPreviewDlg::setMarginsWidth);
    QAction *marginsAct = toolBar->addWidget(marginSpinBox);
    marginsAct->setText(tr("Margins:"));

    toolBar->addSeparator();
    toolBar->addAction(tr("Setup Page"), this, &ScenePrintPreviewDlg::showPageSetupDlg);

    graphView = new BasicGraphView;
    lay->addWidget(graphView);

    previewScene = new PrintPreviewSceneProxy(this);
    graphView->setScene(previewScene);

    //Default to A4 Portraint page
    printerPageLay.setPageSize(QPageSize(QPageSize::A4));
    printerPageLay.setOrientation(QPageLayout::Portrait);
    updateModelPageSize();

    resize(500, 600);
}

bool ScenePrintPreviewDlg::eventFilter(QObject *watched, QEvent *ev)
{
    if(watched == zoomSlider && ev->type() == QEvent::MouseButtonDblClick)
    {
        //Zoom Slider was double clicked, reset zoom level to 100
        updateZoomLevel(100);
    }

    return QDialog::eventFilter(watched, ev);
}

void ScenePrintPreviewDlg::setSourceScene(IGraphScene *sourceScene)
{
    previewScene->setSourceScene(sourceScene);
}

void ScenePrintPreviewDlg::setSceneScale(double scaleFactor)
{
    PrintHelper::PageLayoutOpt pageLay = previewScene->getPageLay();
    if(qFuzzyCompare(pageLay.scaleFactor, scaleFactor))
        return;

    sceneScaleSpinBox->setValue(scaleFactor * 100);
    pageLay.scaleFactor = scaleFactor;
    previewScene->setPageLay(pageLay);
}

QPrinter *ScenePrintPreviewDlg::printer() const
{
    return m_printer;
}

void ScenePrintPreviewDlg::setPrinter(QPrinter *newPrinter)
{
    m_printer = newPrinter;

    if(m_printer)
    {
        //Update page rect
        setPrinterPageLay(m_printer->pageLayout());
    }
}

void ScenePrintPreviewDlg::setPrinterPageLay(const QPageLayout &pageLay)
{
    printerPageLay = pageLay;
    updateModelPageSize();
}

PrintHelper::PageLayoutOpt ScenePrintPreviewDlg::getScenePageLay() const
{
    return previewScene->getPageLay();
}

void ScenePrintPreviewDlg::setScenePageLay(const PrintHelper::PageLayoutOpt &newScenePageLay)
{
    previewScene->setPageLay(newScenePageLay);

    sceneScaleSpinBox->setValue(newScenePageLay.scaleFactor * 100);
    marginSpinBox->setValue(newScenePageLay.marginOriginalWidth);
}

void ScenePrintPreviewDlg::updateZoomLevel(int zoom)
{
    if(graphView->getZoomLevel() == zoom)
        return;

    //Set for view first because it checks minimum and mazimum values
    graphView->setZoomLevel(zoom);

    zoom = graphView->getZoomLevel();
    zoomSlider->setValue(zoom);
    zoomSpinBox->setValue(zoom);
    previewScene->setViewScaleFactor(double(zoom) / 100.0);
}

void ScenePrintPreviewDlg::onScaleChanged(double zoom)
{
    //Convert from 0%-100% to 0-1
    setSceneScale(zoom / 100);
}

void ScenePrintPreviewDlg::setMarginsWidth(double margins)
{
    PrintHelper::PageLayoutOpt pageLay = previewScene->getPageLay();
    if(qFuzzyCompare(pageLay.marginOriginalWidth, margins))
        return;

    marginSpinBox->setValue(margins);
    pageLay.marginOriginalWidth = margins;
    previewScene->setPageLay(pageLay);
}

void ScenePrintPreviewDlg::showPageSetupDlg()
{
    if(m_printer && m_printer->outputFormat() == QPrinter::NativeFormat)
    {
        //For native printers use standard page dialog
        OwningQPointer<QPageSetupDialog> dlg = new QPageSetupDialog(m_printer, this);
        dlg->exec();

        //Update page rect
        setPrinterPageLay(m_printer->pageLayout());
    }
    else
    {
        //Show custom dialog
        OwningQPointer<CustomPageSetupDlg> dlg = new CustomPageSetupDlg(this);
        dlg->setPageSize(printerPageLay.pageSize());
        dlg->setPageOrient(printerPageLay.orientation());
        if(dlg->exec() != QDialog::Accepted || !dlg)
            return;

        printerPageLay.setPageSize(dlg->getPageSize());
        printerPageLay.setOrientation(dlg->getPageOrient());
    }

    updateModelPageSize();
}

void ScenePrintPreviewDlg::updateModelPageSize()
{
    QRect pixelRect = printerPageLay.pageSize().rectPixels(100);
    if(printerPageLay.orientation() == QPageLayout::Landscape) //Transpose for Landscape
        pixelRect = pixelRect.transposed();

    PrintHelper::PageLayoutOpt pageLay = previewScene->getPageLay();
    pageLay.devicePageRect = pixelRect;
    previewScene->setPageLay(pageLay);
}
