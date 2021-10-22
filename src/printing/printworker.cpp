#include "printworker.h"

#include "sceneselectionmodel.h"

#include "graph/model/linegraphscene.h"
#include "graph/view/backgroundhelper.h"

#include <QPainter>

#include <QPrinter>

#include <QSvgGenerator>

#include "info.h"

#include <QDebug>

PrintWorker::PrintWorker(sqlite3pp::database &db, QObject *parent) :
    QObject(parent),
    differentFiles(false),
    outType(Print::Native)
{
    scene = new LineGraphScene(db, this);
}

void PrintWorker::setPrinter(QPrinter *printer)
{
    m_printer = printer;
}

void PrintWorker::setOutputType(Print::OutputType type)
{
    outType = type;
}

void PrintWorker::setFileOutput(const QString &value, bool different)
{
    fileOutput = value;
    if(fileOutput.endsWith('/'))
        fileOutput.chop(1);
    differentFiles = different;
}

void PrintWorker::setSelection(SceneSelectionModel *newSelection)
{
    selection = newSelection;
}

int PrintWorker::getMaxProgress() const
{
    return selection->getSelectionCount();
}

void PrintWorker::doWork()
{
    emit progress(0);

    switch (outType)
    {
    case Print::Native:
    {
        m_printer->setOutputFormat(QPrinter::NativeFormat);
        printPaged();
        break;
    }
    case Print::Pdf:
    {
        printPdf();
        break;
    }
    case Print::Svg:
    {
        printSvg();
        break;
    }
    }

    emit finished();
}

void PrintWorker::printInternal(BeginPaintFunc func, bool endPaintingEveryPage)
{
    QPainter painter(m_printer);

    int progressVal = 0;
    bool firstPage = true;

    if(!selection->startIteration())
        return; //Error

    while (true)
    {
        const SceneSelectionModel::Entry entry = selection->getNextEntry();
        if(!entry.objectId)
            break; //Finished

        emit progress(progressVal++);

        if(!scene->loadGraph(entry.objectId, entry.type))
            continue; //Loading error, skip

        const QRectF sourceRect(QPointF(), scene->getContentSize());

        emit description(scene->getGraphObjectName());

        if(func)
            func(&painter, firstPage, scene->getGraphObjectName(), sourceRect);

        if(firstPage)
            firstPage = false;

        BackgroundHelper::drawBackgroundHourLines(&painter, sourceRect);
        BackgroundHelper::drawStations(&painter, scene, sourceRect);
        BackgroundHelper::drawJobStops(&painter, scene, sourceRect);
        BackgroundHelper::drawJobSegments(&painter, scene, sourceRect);
        BackgroundHelper::drawStationHeader(&painter, scene, sourceRect, 0);

        if(endPaintingEveryPage)
            painter.end();
    }
}

void PrintWorker::printSvg()
{
    QSvgGenerator svg;
    svg.setTitle(QStringLiteral("Timetable Session"));
    svg.setDescription(QStringLiteral("Generated by %1").arg(AppDisplayName));

    const QString fmt = QString("%1/%2.svg").arg(fileOutput);

    auto beginPaint = [&svg, &fmt](QPainter *painter, bool firstPage,
                                   const QString& title, const QRectF& sourceRect)
    {
        svg.setSize(sourceRect.size().toSize());
        svg.setViewBox(sourceRect);

        svg.setFileName(fmt.arg(title));

        if(!painter->begin(&svg))
            qWarning() << "PrintWorker::printSvg(): cannot begin QPainter";
    };

    printInternal(beginPaint, true);
}

void PrintWorker::printPdf()
{
    m_printer->setOutputFormat(QPrinter::PdfFormat);
    m_printer->setCreator(AppDisplayName);
    m_printer->setDocName(QStringLiteral("Timetable Session"));

    if(differentFiles)
    {
        printPdfMultipleFiles();
    }
    else
    {
        m_printer->setOutputFileName(fileOutput);
        printPaged();
    }
}

void PrintWorker::printPdfMultipleFiles()
{
    const QString fmt = QString("%1/%2.pdf").arg(fileOutput);

    auto beginPaint = [printer = m_printer, &fmt](QPainter *painter, bool firstPage,
                                   const QString& title, const QRectF& sourceRect)
    {
        printer->setOutputFileName(fmt.arg(title));
        if(!painter->begin(printer))
            qWarning() << "PrintWorker::printPdfMultipleFiles(): cannot begin QPainter";
    };

    printInternal(beginPaint, true);
}

void PrintWorker::printPaged()
{
    auto beginPaint = [printer = m_printer](QPainter *painter, bool firstPage,
                                                  const QString& title, const QRectF& sourceRect)
    {
        if(firstPage)
        {
            if(!painter->begin(printer))
                qWarning() << "PrintWorker::printPaged(): cannot begin QPainter";
        }
        else
        {
            if(!printer->newPage())
                qWarning() << "PrintWorker::printPaged(): cannot add new page";
        }
    };

    printInternal(beginPaint, false);
}
