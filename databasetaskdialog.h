#ifndef DATABASETASKDIALOG_H
#define DATABASETASKDIALOG_H
#include "databasejob.h"
#include <QDialog>
#include <QElapsedTimer>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTimer;

// goal: Review a captured database target and show a cancellable maintenance task.
// pre: GUI-thread use. The main window handles startRequested, closes its own target
//      connections if needed, and then calls begin(). Credentials stay in memory.
// post: No tool runs before Start. Drop additionally requires the exact database name.
// internals: Concrete controls configure DatabaseJob; its output fills a bounded log.
//            A monotonic elapsed timer includes the process and final backup file commit.
//            The dialog is modeless and cannot disappear while its job is still running.
class DatabaseTaskDialog : public QDialog
{
    Q_OBJECT
public:
    DatabaseTaskDialog(DatabaseJob::Operation operation, const QVariantMap &profile,
                       const QString &password, const QString &database, QWidget *parent = nullptr);
    bool isRunning() const { return job->isRunning(); }
    QString database() const;
    // Run after the owner has prepared the captured target. Invalid input starts no job.
    void begin();
    void reject() override;
signals:
    void startRequested();
    void completed(bool success);
    // Open server-wide role management, optionally suggesting a missing role from the log.
    void rolesRequested(const QString &name);
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void browse();
    void updateControls();
    void appendLog(const QString &text);
    void updateDuration();
    DatabaseJob::Operation operation;
    QVariantMap profile;
    QString password;
    QString target;
    DatabaseJob *job;
    QLineEdit *name;
    QLineEdit *file;
    QLineEdit *confirmation;
    QComboBox *format;
    QCheckBox *clean;
    QCheckBox *skipOwners;
    QPushButton *browseButton;
    QPushButton *start;
    QPushButton *close;
    QPushButton *roles;
    QProgressBar *progress;
    QLabel *status;
    QLabel *duration;
    QTimer *durationTimer;
    QElapsedTimer elapsed;
    QPlainTextEdit *log;
    bool started = false;
};
#endif
