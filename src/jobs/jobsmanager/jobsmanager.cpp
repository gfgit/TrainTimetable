/*
 * ModelRailroadTimetablePlanner
 * Copyright 2016-2023, Filippo Gentile
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "jobsmanager.h"

#include <QDebug>

#include <QVBoxLayout>
#include <QTableView>
#include "utils/delegates/sql/filterheaderview.h"
#include <QToolBar>

#include "model/joblistmodel.h"
#include "model/jobshelper.h"
#include "utils/jobcategorystrings.h"

#include "utils/delegates/sql/modelpageswitcher.h"

#include "app/session.h"
#include "viewmanager/viewmanager.h"

#include <QMessageBox>
#include "newjobsamepathdlg.h"
#include "utils/owningqpointer.h"

JobsManager::JobsManager(QWidget *parent) :
    QWidget(parent)
{
    QVBoxLayout *l = new QVBoxLayout(this);
    setMinimumSize(750, 300);

    QToolBar *toolBar  = new QToolBar(this);
    QAction *actNewJob = toolBar->addAction(tr("New Job"), this, &JobsManager::onNewJob);
    actRemoveJob       = toolBar->addAction(tr("Remove"), this, &JobsManager::onRemove);
    actNewJobSamePath =
      toolBar->addAction(tr("New Same Path"), this, &JobsManager::onNewJobSamePath);
    toolBar->addSeparator();
    actEditJob        = toolBar->addAction(tr("Edit"), this, &JobsManager::onEditJob);
    actShowJobInGraph = toolBar->addAction(tr("Show Graph"), this, &JobsManager::onShowJobGraph);
    toolBar->addSeparator();
    QAction *actRemoveAll =
      toolBar->addAction(tr("Remove All"), this, &JobsManager::onRemoveAllJobs);
    l->addWidget(toolBar);

    view = new QTableView;
    view->setSelectionMode(QTableView::SingleSelection);
    connect(view, &QTableView::doubleClicked, this, &JobsManager::editJobAtRow);
    l->addWidget(view);

    // Custom sorting and filtering
    FilterHeaderView *header = new FilterHeaderView(view);
    header->installOnTable(view);

    jobsModel = new JobListModel(Session->m_Db, this);
    view->setModel(jobsModel);

    auto ps = new ModelPageSwitcher(false, this);
    l->addWidget(ps);
    ps->setModel(jobsModel);

    connect(view->selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &JobsManager::onSelectionChanged);
    connect(jobsModel, &QAbstractItemModel::modelReset, this, &JobsManager::onSelectionChanged);

    jobsModel->refreshData();

    // Action Tooltips
    actNewJob->setToolTip(tr("Create new Job and open Job Editor"));
    actRemoveJob->setToolTip(tr("Delete selected Job"));
    actNewJobSamePath->setToolTip(tr("Create new Job with same path of selected one"));
    actEditJob->setToolTip(tr("Open selected Job in Job Editor.<br>"
                              "<b>You can double click on a row to edit Job.</b>"));
    actShowJobInGraph->setToolTip(tr("Show selected Job in graph"));
    actRemoveAll->setToolTip(tr("Delete all Jobs of this session"));

    setWindowTitle("Jobs Manager");
}

void JobsManager::editJobAtRow(const QModelIndex &idx)
{
    db_id jobId = jobsModel->getIdAtRow(idx.row());
    if (!jobId)
        return;
    Session->getViewManager()->requestJobEditor(jobId);
}

void JobsManager::onNewJob()
{
    Session->getViewManager()->requestJobCreation();
}

void JobsManager::onRemove()
{
    QModelIndex idx = view->currentIndex();
    if (!idx.isValid())
        return;

    db_id jobId        = jobsModel->getIdAtRow(idx.row());
    JobCategory jobCat = jobsModel->getShiftAnCatAtRow(idx.row()).second;
    QString jobName    = JobCategoryName::jobName(jobId, jobCat);

    int ret            = QMessageBox::question(this, tr("Job deletion"),
                                               tr("Are you sure to delete job %1?").arg(jobName),
                                               QMessageBox::Yes | QMessageBox::Cancel);
    if (ret == QMessageBox::Yes)
    {
        if (!JobsHelper::removeJob(Session->m_Db, jobId))
        {
            qWarning() << "Error while deleting job:" << jobId << "from JobManager"
                       << Session->m_Db.error_msg();
            // ERRORMSG: message box or statusbar
        }
    }
}

void JobsManager::onRemoveAllJobs()
{
    int ret = QMessageBox::question(
      this, tr("Delete all jobs?"),
      tr("Are you really sure you want to delete all jobs from this session?"));
    if (ret == QMessageBox::Yes)
        JobsHelper::removeAllJobs(Session->m_Db);
}

void JobsManager::onNewJobSamePath()
{
    QModelIndex idx = view->currentIndex();
    if (!idx.isValid())
        return;

    db_id jobId = jobsModel->getIdAtRow(idx.row());
    if (!jobId)
        return;
    JobCategory jobCat                    = jobsModel->getShiftAnCatAtRow(idx.row()).second;
    auto times                            = jobsModel->getOrigAndDestTimeAtRow(idx.row());

    OwningQPointer<NewJobSamePathDlg> dlg = new NewJobSamePathDlg(this);
    dlg->setSourceJob(jobId, jobCat, times.first, times.second);

    if (dlg->exec() != QDialog::Accepted || !dlg)
        return;

    const QTime newStart = dlg->getNewStartTime();
    const int secsOffset = times.first.secsTo(newStart);

    db_id newJobId       = 0;
    if (!JobsHelper::createNewJob(Session->m_Db, newJobId, jobCat))
        return;

    JobsHelper::copyStops(Session->m_Db, jobId, newJobId, secsOffset, dlg->shouldCopyRs(),
                          dlg->shouldReversePath());

    // Let user edit newly created job
    Session->getViewManager()->requestJobEditor(newJobId);
}

void JobsManager::onEditJob()
{
    editJobAtRow(view->currentIndex());
}

void JobsManager::onShowJobGraph()
{
    QModelIndex idx = view->currentIndex();
    if (!idx.isValid())
        return;

    db_id jobId = jobsModel->getIdAtRow(idx.row());
    if (!jobId)
        return;

    Session->getViewManager()->requestJobSelection(jobId, true, true);

    // Minimize JobsManager to make graph visible
    showMinimized();
}

void JobsManager::onSelectionChanged()
{
    const bool hasSel = view->selectionModel()->hasSelection();

    actRemoveJob->setEnabled(hasSel);
    actNewJobSamePath->setEnabled(hasSel);
    actEditJob->setEnabled(hasSel);
    actShowJobInGraph->setEnabled(hasSel);
}
