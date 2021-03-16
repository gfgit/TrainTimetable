#include "appsettings.h"

#include "utils/types.h"

#include <QApplication>

#include <QColor>

#include <QDebug>

TrainTimetableSettings::TrainTimetableSettings(QObject *parent) :
    QObject(parent)
{

}

void TrainTimetableSettings::loadSettings(const QString& fileName)
{
    m_settings.reset(new QSettings(fileName, QSettings::IniFormat));
    qDebug() << "SETTINGS:" << m_settings->fileName() << m_settings->isWritable();
}

void TrainTimetableSettings::saveSettings()
{
    if(m_settings)
        m_settings->sync();
}

void TrainTimetableSettings::restoreDefaultSettings()
{
    if(!m_settings)
        return;

    m_settings->clear();
}

QColor TrainTimetableSettings::getCategoryColor(int category)
{
    if(!m_settings || category >= int(JobCategory::NCategories) || category < 0)
        return QColor(); //Invalid Category

    QRgb defaultColors[int(JobCategory::NCategories)] = { //NOTE: keep in sync wiht JobCategory
        0x00FFFF, //FREIGHT   (MERCI)
        0x0000FF, //LIS       (LIS)
        0x808000, //POSTAL    (POSTALE)

        0x008000, //REGIONAL  (REGIONALE)
        0x005500, //FAST_REGIONAL  (REGIONALE VELOCE)
        0x00FF00, //LOCAL     (LOCALE)
        0xFFAA00, //INTERCITY (INTERCITY)
        0x800080, //EXPRESS   (ESPRESSO)
        0x800000, //DIRECT    (DIRETTO)
        0xFF0000, //HIGH_SPEED (ALTA VELOCITA')
    };

    //TODO: maybe use english category names in key instead of enum index
    const QString key = QStringLiteral("job_colors/category_") + QString::number(category);

    return utils::fromVariant<QColor>(m_settings->value(key, QColor(defaultColors[category])));
}

void TrainTimetableSettings::setCategoryColor(int category, const QColor &color)
{
    if(!m_settings || category >= int(JobCategory::NCategories) || category < 0 || !color.isValid())
        return; //Invalid Category

    //TODO: maybe use english category names in key instead of enum index
    const QString key = QStringLiteral("job_colors/category_") + QString::number(category);

    m_settings->setValue(key, utils::toVariant<QColor>(color));
}

int TrainTimetableSettings::getDefaultStopMins(int category)
{
    if(!m_settings || category >= int(JobCategory::NCategories) || category < 0)
        return -1; //Invalid Category

    //0 minutes means transit

    quint8 defaultTimesArr[int(JobCategory::NCategories)] = { //NOTE: keep in sync wiht JobCategory
        10, //FREIGHT   (MERCI)
        10, //LIS       (LIS)
        10, //POSTAL    (POSTALE)

        2,  //REGIONAL  (REGIONALE)
        2,  //FAST_REGIONAL  (REGIONALE VELOCE)
        2,  //LOCAL     (LOCALE)
        0,  //INTERCITY (INTERCITY)
        0,  //EXPRESS   (ESPRESSO)
        0,  //DIRECT    (DIRETTO)
        0,  //HIGH_SPEED (ALTA VELOCITA')
    };

    //TODO: maybe use english category names in key instead of enum index
    const QString key = QStringLiteral("stops/default_stop_mins_") + QString::number(category);
    return m_settings->value(key, defaultTimesArr[category]).toInt();
}

void TrainTimetableSettings::setDefaultStopMins(int category, int mins)
{
    if(!m_settings || category >= int(JobCategory::NCategories) || category < 0 || mins < 0)
        return; //Invalid Category

    //TODO: maybe use english category names in key instead of enum index
    const QString key = QStringLiteral("stops/default_stop_mins_") + QString::number(category);
    m_settings->setValue(key, mins);
}

QFont TrainTimetableSettings::getFontHelper(const QString &baseKey, QFont defFont)
{
    //NOTE: split into 2 keys: family, pt
    if(!m_settings)
        return defFont;

    const int pt_size = m_settings->value(baseKey + QLatin1String("_pt")).toInt();
    const QString family = m_settings->value(baseKey + QLatin1String("_family")).toString();

    if(pt_size != 0)
        defFont.setPointSize(pt_size);
    if(!family.isEmpty())
        defFont.setFamily(family);
    return defFont;
}

void TrainTimetableSettings::setFontHelper(const QString &baseKey, const QFont &f)
{
    if(!m_settings)
        return;

    m_settings->setValue(baseKey + QLatin1String("_pt"), f.pointSize());
    m_settings->setValue(baseKey + QLatin1String("_family"), f.family());
}
