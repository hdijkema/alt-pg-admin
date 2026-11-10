#ifndef CATALOGSERVICE_H
#define CATALOGSERVICE_H
#include <QObject>
#include <QHash>
#include <QQueue>
#include <QVariantMap>

class PgSession;
// goal: Read catalog branches and object properties outside user query sessions.
// pre: Request on the GUI thread with an authorised profile and explicit database.
// post: Each request emits loaded(id, rows) or failed(id, message), unless its
//       server is closed or this service is destroyed.
// internals: Per-database queues share at most eight PgSessions. Only idle entries
//            are evicted. SQL is fixed catalog SELECTs with escaped name literals.
class CatalogService : public QObject
{
    Q_OBJECT
public:
    enum Kind { Databases, Schemas, Relations, Columns, DatabaseProperties,
                SchemaProperties, RelationProperties, ColumnProperties };
    explicit CatalogService(QObject *parent = nullptr);
    ~CatalogService() override;
    // Queue one SELECT and return its ID immediately; rows use SQL column names as keys.
    quint64 request(const QVariantMap &profile, const QString &password, Kind kind,
                    const QString &schema = QString(), const QString &relation = QString(),
                    const QString &column = QString());
    void closeServer(const QString &profileId);
    // Close only this database's metadata connection before restore/drop; other queues survive.
    void closeDatabase(const QString &profileId, const QString &database);
    // Build a catalog SELECT for the requested kind; never interpolate SQL identifiers.
    static QString sql(Kind kind, const QString &schema, const QString &relation,
                       const QString &column = QString());
signals:
    void loaded(quint64 request, const QVariantList &rows);
    void failed(quint64 request, const QString &message);
private:
    struct Request { quint64 id; QString sql; };
    struct Connection {
        PgSession *session = nullptr;
        QString profileId;
        QQueue<Request> requests;
        QVariantList rows;
        QString error;
    };
    void startNext(const QString &key);
    QHash<QString, Connection> connections;
    quint64 nextRequest = 1;
};
#endif
