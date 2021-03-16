#include "rscoupledialog.h"

#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QFrame>

#include <QGridLayout>

#include "model/rsproxymodel.h"

#include "utils/rs_utils.h"

#include <sqlite3pp/sqlite3pp.h>

RSCoupleDialog::RSCoupleDialog(RSCouplingInterface *mgr, RsOp o, QWidget *parent) :
    QDialog (parent),
    couplingMgr(mgr),
    legend(nullptr),
    op(o)
{
    engModel     = new RSProxyModel(couplingMgr, op, RsType::Engine, this);
    coachModel   = new RSProxyModel(couplingMgr, op, RsType::Coach, this);
    freightModel = new RSProxyModel(couplingMgr, op, RsType::FreightWagon, this);

    QGridLayout *lay = new QGridLayout(this);

    QFont f;
    f.setBold(true);
    f.setPointSize(10);

    QListView *engView = new QListView;
    engView->setModel(engModel);
    QLabel *engLabel = new QLabel(tr("Engines"));
    engLabel->setAlignment(Qt::AlignCenter);
    engLabel->setFont(f);
    lay->addWidget(engLabel, 0, 0);
    lay->addWidget(engView, 1, 0);

    QListView *coachView = new QListView;
    coachView->setModel(coachModel);
    QLabel *coachLabel = new QLabel(tr("Coaches"));
    coachLabel->setAlignment(Qt::AlignCenter);
    coachLabel->setFont(f);
    lay->addWidget(coachLabel, 0, 1);
    lay->addWidget(coachView, 1, 1);

    QListView *freightView = new QListView;
    freightView->setModel(freightModel);
    QLabel *freightLabel = new QLabel(tr("Freight Wagons"));
    freightLabel->setAlignment(Qt::AlignCenter);
    freightLabel->setFont(f);
    lay->addWidget(freightLabel, 0, 2);
    lay->addWidget(freightView, 1, 2);

    showHideLegendBut = new QPushButton;
    lay->addWidget(showHideLegendBut, 2, 0, 1, 1);

    legend = new QFrame;
    lay->addWidget(legend, 3, 0, 1, 3);
    legend->hide();

    QDialogButtonBox *box = new QDialogButtonBox(QDialogButtonBox::Ok); //TODO: implement also cancel
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(box, 4, 0, 1, 3);

    connect(showHideLegendBut, &QPushButton::clicked, this, &RSCoupleDialog::toggleLegend);
    updateButText();

    setMinimumSize(400, 300);
    setWindowFlag(Qt::WindowMaximizeButtonHint);
}

void RSCoupleDialog::loadProxyModels(sqlite3pp::database& db, db_id jobId, db_id stopId, db_id stationId, const QTime& arrival)
{
    QVector<RSProxyModel::RsItem> engines, freight, coaches;

    sqlite3pp::query q(db);

    if(op == Coupled)
    {
        /* Show Couple-able RS:
         * - RS free in this station
         * - Unused RS (green)
         * - RS without operations before this time (=arrival) (cyan)
         *
         * Plus:
         * - Possible wrong operations to let the user remove them
         */

        q.prepare("SELECT MAX(sub.p), sub.rsId, rs_list.number, rs_models.name, rs_models.suffix, rs_models.type, rs_models.sub_type, sub.arr, sub.jobId, jobs.category FROM ("

                  //Select possible wrong operations to let user remove (un-check) them
                  " SELECT 1 AS p, coupling.rsId AS rsId, NULL AS arr, NULL AS jobId FROM coupling WHERE coupling.stopId=?3 AND coupling.operation=1"
                  " UNION ALL"

                  //Select RS uncoupled before our arrival (included RS uncoupled at exact same time) (except uncoupled by us)
                  " SELECT 2 AS p, coupling.rsId AS rsId, MAX(stops.arrival) AS arr, stops.jobId AS jobId"
                  " FROM stops"
                  " JOIN coupling ON coupling.stopId=stops.id"
                  " WHERE stops.stationId=?1 AND stops.arrival <= ?2 AND stops.id<>?3"
                  " GROUP BY coupling.rsId"
                  " HAVING coupling.operation=0"
                  " UNION ALL"

                  //Select RS coupled after our arrival (excluded RS coupled at exact same time)
                  " SELECT 3 AS p, coupling.rsId, MIN(stops.arrival) AS arr, stops.jobId AS jobId"
                  " FROM coupling"
                  " JOIN stops ON stops.id=coupling.stopId"
                  " WHERE stops.stationId=?1 AND stops.arrival > ?2"
                  " GROUP BY coupling.rsId"
                  " HAVING coupling.operation=1"
                  " UNION ALL"

                  //Select coupled RS for first time
                  " SELECT 4 AS p, rs_list.id AS rsId, NULL AS arr, NULL AS jobId"
                  " FROM rs_list"
                  " WHERE NOT EXISTS ("
                  " SELECT coupling.rsId FROM coupling"
                  " JOIN stops ON stops.id=coupling.stopId WHERE coupling.rsId=rs_list.id AND stops.arrival<?2)"
                  " UNION ALL"

                  //Select unused RS (RS without any operation)
                  " SELECT 5 AS p, rs_list.id AS rsId, NULL AS arr, NULL AS jobId"
                  " FROM rs_list"
                  " WHERE NOT EXISTS (SELECT coupling.rsId FROM coupling WHERE coupling.rsId=rs_list.id)"
                  " UNION ALL"

                  //Select RS coupled before our arrival (already coupled by this job or occupied by other job)
                  " SELECT 7 AS p, c1.rsId, MAX(s1.arrival) AS arr, s1.jobId AS jobId"
                  " FROM coupling c1"
                  " JOIN coupling c2 ON c2.rsId=c1.rsId"
                  " JOIN stops s1 ON s1.id=c2.stopId"
                  " WHERE c1.stopId=?3 AND c1.operation=1 AND s1.arrival<?2"
                  " GROUP BY c1.rsId"
                  " HAVING c2.operation=1"
                  " ) AS sub"

                  " JOIN rs_list ON rs_list.id=sub.rsId"    //FIXME: it seems it is better to join in the subquery directly, also avoids some LEFT in joins
                  " LEFT JOIN rs_models ON rs_models.id=rs_list.model_id"
                  " LEFT JOIN jobs ON jobs.id=sub.jobId"
                  " GROUP BY sub.rsId"
                  " ORDER BY rs_models.name, rs_list.number");

        q.bind(1, stationId);
        q.bind(2, arrival);
        q.bind(3, stopId);
    }
    else
    {
        /*Show Uncouple-able:
         * - All RS coupled up to now (train asset before this stop)
         *
         * Plus:
         * - Show possible wrong operations to let user remove them
         */

        q.prepare("SELECT MAX(sub.p), sub.rsId, rs_list.number, rs_models.name, rs_models.suffix, rs_models.type, rs_models.sub_type, sub.arr, NULL AS jobId, NULL AS category FROM("
                  "SELECT 8 AS p, coupling.rsId AS rsId, MAX(stops.arrival) AS arr"
                  " FROM stops"
                  " JOIN coupling ON coupling.stopId=stops.id"
                  " WHERE stops.arrival<?2"
                  " GROUP BY coupling.rsId"
                  " HAVING coupling.operation=1 AND stops.jobId=?1"
                  " UNION ALL"

                  //Select possible wrong operations to let user remove them
                  " SELECT 6 AS p, coupling.rsId AS rsId, NULL AS arr FROM coupling WHERE coupling.stopId=?3 AND coupling.operation=0"
                  ") AS sub"
                  " JOIN rs_list ON rs_list.id=sub.rsId"    //FIXME: it seems it is better to join in the subquery directly, also avoids some LEFT in joins
                  " LEFT JOIN rs_models ON rs_models.id=rs_list.model_id"
                  " GROUP BY sub.rsId"
                  " ORDER BY rs_models.name, rs_list.number");

        q.bind(1, jobId);
        q.bind(2, arrival);
        q.bind(3, stopId);
    }

    for(auto rs : q)
    {
        /*Priority flag:
         * 1: The RS is not free in this station, it's shown to let the user remove the operation
         * 2: The RS is free and has no following operation in this station
         *    (It could have wrong operation in other station that should be fixed by user,
         *     as shown in RsErrorWidget)
         * 3: The RS is free but a job will couple it in the future so you should bring it back here before that time
         * 4: The RS is used for first time
         * 5: The RS is unsed
         * 6: RS was not coupled before this stop and you are trying to uncouple it
         * 7: Normal RS uncouple-able, used to win against 6
        */

        RSProxyModel::RsItem item;
        item.flag = rs.get<int>(0);
        item.rsId = rs.get<db_id>(1);

        int number = rs.get<int>(2);
        int modelNameLen = sqlite3_column_bytes(q.stmt(), 3);
        const char *modelName = reinterpret_cast<char const*>(sqlite3_column_text(q.stmt(), 3));

        int modelSuffixLen = sqlite3_column_bytes(q.stmt(), 4);
        const char *modelSuffix = reinterpret_cast<char const*>(sqlite3_column_text(q.stmt(), 4));
        RsType type = RsType(rs.get<int>(5));
        RsEngineSubType subType = RsEngineSubType(rs.get<int>(6));

        item.rsName = rs_utils::formatNameRef(modelName, modelNameLen,
                                                       number,
                                                       modelSuffix, modelSuffixLen,
                                                       type);

        item.engineType = RsEngineSubType::Invalid;

        item.time = rs.get<QTime>(7);
        item.jobId = rs.get<db_id>(8);
        item.jobCat = JobCategory(rs.get<int>(9));

        switch (type)
        {
        case RsType::Engine:
        {
            item.engineType = subType;
            engines.append(item);
            break;
        }
        case RsType::FreightWagon:
        {
            freight.append(item);
            break;
        }
        case RsType::Coach:
        {
            coaches.append(item);
            break;
        }
        default:
            break;
        }
    }

    engModel->loadData(engines);
    freightModel->loadData(freight);
    coachModel->loadData(coaches);
}

void RSCoupleDialog::toggleLegend()
{
    if(legend->isVisible())
    {
        legend->hide();
    }else{
        legend->show();
        if(!legend->layout())
        {
            QVBoxLayout *legendLay = new QVBoxLayout(legend);
            QLabel *label = new QLabel(tr("<p style=\"font-size:13pt\">"
                                          "<span style=\"background-color:#FF56FF\">___</span> The item isn't coupled before or already coupled.<br>"
                                          "<span style=\"background-color:#FF3d43\">___</span> The item isn't in this station.<br>"
                                          "<span style=\"color:#0000FF;background-color:#FFFFFF\">\\\\\\\\</span> Railway line doesn't allow electric traction.<br>"
                                          "<span style=\"background-color:#00FFFF\">___</span> First use of this item.<br>"
                                          "<span style=\"background-color:#00FF00\">___</span> This item is never used in this session.</p>"));
            label->setTextFormat(Qt::RichText);
            legendLay->addWidget(label);
            legend->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
        }
    }
    updateButText();
    adjustSize();
}

void RSCoupleDialog::updateButText()
{
    showHideLegendBut->setText(legend->isVisible() ? tr("Hide legend") : tr("Show legend"));
}
