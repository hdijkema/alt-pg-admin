#ifndef DATABASEJOB_H
#define DATABASEJOB_H
#include <QObject>
#include <QVariantMap>
#include <QFuture>
#include <QStringDecoder>
#include <atomic>
#include <memory>

class QProcess;
class QTemporaryFile;
class PgSession;

// goal: Run database backup/restore/create/drop or VACUUM/ANALYZE/REINDEX without blocking the GUI.
// pre: Call on the GUI thread, while idle. The caller authorises the captured target
//      and releases its own target connections before restore/drop.
// post: finished reports the tool's actual outcome. Failed/cancelled backups preserve
//       an existing destination; restore is transactional for standard pg_dump scripts.
// internals: QProcess runs native PostgreSQL tools with private credentials and bounded
//            logs. Successful backups are copied from a temporary file through QSaveFile
//            on QtConcurrent. An atomic cancellation flag also stops that final copy.
//            Maintenance uses its own asynchronous PgSession and native libpq cancellation.
class DatabaseJob : public QObject
{
    Q_OBJECT
public:
    enum Operation { Backup, Restore, Create, Drop, Vacuum, Analyze, Reindex };
    explicit DatabaseJob(QObject *parent = nullptr);
    ~DatabaseJob() override;
    bool isRunning() const { return running; }
    // Start a full database backup, custom archive by default or a plain psql script.
    void backup(const QVariantMap &profile, const QString &password, const QString &database,
                const QString &filename, bool plainSql = false);
    // Restore a custom/tar archive with pg_restore, otherwise a SQL file with psql.
    // clean/skipOwners apply to archives only; SQL scripts define their own commands.
    void restore(const QVariantMap &profile, const QString &password, const QString &database,
                 const QString &filename, bool clean = false, bool skipOwners = true);
    // Create a UTF-8 database from template0, owned by the connecting user.
    void create(const QVariantMap &profile, const QString &password, const QString &database);
    // Drop one non-maintenance, non-system database, without FORCE or IF EXISTS.
    void drop(const QVariantMap &profile, const QString &password, const QString &database);
    // Maintain a whole database or a quoted schema/relation on a private autocommit session.
    // No user query transaction is touched. Cancelling may leave already completed work in place.
    void maintain(Operation operation, const QVariantMap &profile, const QString &password,
                  const QString &database, const QString &schema = {}, const QString &relation = {},
                  bool vacuumAnalyze = true, bool reindexConcurrently = true);
    // Build the same SQL shown in the dialog. Invalid operation/target returns empty SQL and error.
    static QString maintenanceSql(Operation operation, const QString &database, const QString &schema,
                                  const QString &relation, bool vacuumAnalyze, bool reindexConcurrently, QString *error);
    // Request cancellation; CREATE/DROP may already have committed and cannot be undone.
    void cancel();
    // Inspect archive magic, not its extension. Directory archives are not selected by the UI.
    static bool isArchive(const QString &filename);
    // Return a concrete validation/protection error, or an empty string when allowed.
    static QString nameError(const QString &database);
    static QString dropError(const QVariantMap &profile, const QString &database);
signals:
    void output(const QString &text);
    void finished(bool success, const QString &message);
private:
    void launch(const QString &tool, const QVariantMap &profile, const QString &password, const QStringList &arguments);
    void readOutput();
    void complete(bool success, const QString &message);
    void commitBackup();
    QProcess *process = nullptr;
    PgSession *maintenance = nullptr;
    QTemporaryFile *temporary = nullptr;
    QFuture<QString> copyFuture;
    std::shared_ptr<std::atomic_bool> cancelled;
    QStringDecoder decoder{QStringDecoder::Utf8};
    QString destination;
    bool running = false;
    Operation operation = Backup;
};
#endif
