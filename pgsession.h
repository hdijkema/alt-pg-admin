#ifndef PGSESSION_H
#define PGSESSION_H

#include <QObject>
#include <QThread>
#include <QVariantMap>
#include "queryresult.h"

class PgConnection;

// goal: Provide asynchronous PostgreSQL access to the GUI without sharing PGconn.
// pre: Call public methods on the owning GUI thread. One script may run at a time.
// post: State/result signals describe completion; no method blocks for server I/O.
// internals: Commands are queued to PgConnection's worker thread. Batches travel
//            back through resultReady, then acknowledged() releases backpressure.
class PgSession : public QObject
{
    Q_OBJECT
public:
    enum State { Disconnected, Connecting, Connected, Disconnecting };
    Q_ENUM(State)
    explicit PgSession(QObject *parent = nullptr);
    ~PgSession() override;
    // Begin connecting only from Disconnected; completion arrives through connected/failed.
    void open(const QVariantMap &profile, const QString &password = QString());
    // Request asynchronous shutdown; closing a busy connection ends that execution.
    void close();
    // Accept a script only while connected and idle. Returns whether it was queued,
    // not whether SQL succeeded; executionFinished supplies that result and transaction state.
    bool execute(const QString &sql);
    // Apply role statements and a new password in one transaction. sql contains
    // no transaction commands or password. The worker builds a SCRAM verifier
    // with libpq before sending ALTER ROLE; plaintext never enters SQL/results.
    bool executeRoleChange(const QString &sql, const QString &role, const QString &password);
    // Request cancellation of the current script; PostgreSQL confirms it asynchronously.
    void cancel();
    bool isBusy() const { return busy; }
    State state() const { return connectionState; }
    QVariantMap information() const { return serverInformation; }

signals:
    void stateChanged();
    void connected(const QVariantMap &information);
    void failed(const QString &message, bool needsPassword);
    void disconnected();
    void openRequested(const QVariantMap &profile, const QString &password);
    void closeRequested();
    void executeRequested(const QString &sql);
    void roleChangeRequested(const QString &sql, const QString &role, const QString &password);
    void cancelRequested();
    void acknowledged();
    void resultReady(const QueryResult &result);
    void notice(const QString &message);
    void executionFinished(bool success, const QString &transactionStatus);

private:
    QThread thread;
    PgConnection *worker;
    State connectionState = Disconnected;
    QVariantMap serverInformation;
    bool busy = false;
};

#endif
