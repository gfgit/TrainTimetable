#ifndef PRINTPROGRESSPAGE_H
#define PRINTPROGRESSPAGE_H

#include <QWizardPage>

#include <QThread>

class PrintWizard;
class PrintWorker;
class QLabel;
class QProgressBar;

class PrintProgressPage : public QWizardPage
{
    Q_OBJECT
public:
    PrintProgressPage(PrintWizard *w, QWidget *parent = nullptr);
    ~PrintProgressPage();

    void initializePage() override;
    bool validatePage() override;
    bool isComplete() const override;

private slots:
    void handleFinished();
    void handleProgress(int val);
    void handleDescription(const QString &text);
    void handleError(const QString &text);
    void setupWorker();

private:
    PrintWizard *mWizard;

    QLabel *m_label;
    QProgressBar *m_progressBar;

    PrintWorker *m_worker;
    QThread m_thread;
    bool complete;
};

#endif // PRINTPROGRESSPAGE_H
