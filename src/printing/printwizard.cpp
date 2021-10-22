#include "printwizard.h"

#include "selectionpage.h"
#include "fileoptionspage.h"
#include "printeroptionspage.h"
#include "progresspage.h"

#include "sceneselectionmodel.h"

#include <QPrinter>

PrintWizard::PrintWizard(sqlite3pp::database &db, QWidget *parent) :
    QWizard (parent),
    mDb(db),
    differentFiles(false),
    type(Print::Native)
{
    printer = new QPrinter;
    selectionModel = new SceneSelectionModel(mDb, this);

    setPage(0, new PrintSelectionPage(this));
    setPage(1, new PrintFileOptionsPage(this));
    setPage(2, new PrinterOptionsPage(this));
    setPage(3, new PrintProgressPage(this));

    setWindowTitle(tr("Print Wizard"));
}

PrintWizard::~PrintWizard()
{
    delete printer;
}

Print::OutputType PrintWizard::getOutputType() const
{
    return type;
}

void PrintWizard::setOutputType(Print::OutputType out)
{
    type = out;
}

QString PrintWizard::getOutputFile() const
{
    return fileOutput;
}

void PrintWizard::setOutputFile(const QString &fileName)
{
    fileOutput = fileName;
}

bool PrintWizard::getDifferentFiles() const
{
    return differentFiles;
}

void PrintWizard::setDifferentFiles(bool newDifferentFiles)
{
    differentFiles = newDifferentFiles;
}

QPrinter *PrintWizard::getPrinter() const
{
    return printer;
}
