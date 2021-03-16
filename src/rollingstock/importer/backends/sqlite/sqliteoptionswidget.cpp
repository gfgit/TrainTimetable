#include "sqliteoptionswidget.h"

#include <QVBoxLayout>
#include <QLabel>

SQLiteOptionsWidget::SQLiteOptionsWidget(QWidget *parent) :
    IOptionsWidget(parent)
{
    //SQLite Option
    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("Import rollingstock pieces, models and owners from another Train Timetable session file.\n"
                                 "The file must be a valid Train Timetable session of recent version\n"
                                 "Extension: (*.ttt)")));
    lay->setAlignment(Qt::AlignTop | Qt::AlignRight);
}

void SQLiteOptionsWidget::loadSettings(const QMap<QString, QVariant> &/*settings*/)
{

}

void SQLiteOptionsWidget::saveSettings(QMap<QString, QVariant> &/*settings*/)
{

}
