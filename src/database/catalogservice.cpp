#include "catalogservice.h"
#include "pgsession.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
#include <QPointer>

// Create a metadata queue without opening any database connection.
CatalogService::CatalogService(QObject *parent) : QObject(parent) {}
CatalogService::~CatalogService() { for (const auto &entry : connections) delete entry.session; }

// Build one fixed catalog SELECT, escaping user object names as literals rather than SQL identifiers.
QString CatalogService::sql(Kind kind, const QString &schema, const QString &relation, const QString &column)
{
    auto literal = [](QString value) { value.replace("'", "''"); return "'" + value + "'"; };
    switch (kind) {
    case Databases:
        return "SELECT datname AS name, datallowconn AND pg_catalog.has_database_privilege(oid, 'CONNECT') AS accessible, datistemplate AS is_template "
               "FROM pg_catalog.pg_database WHERE NOT datistemplate ORDER BY datname";
    case Schemas:
        return "SELECT nspname AS name FROM pg_catalog.pg_namespace WHERE nspname !~ '^pg_' AND nspname <> 'information_schema' "
               "AND pg_catalog.has_schema_privilege(oid, 'USAGE') ORDER BY nspname";
    case Relations:
        return "SELECT c.relname AS name, c.relkind::text AS kind FROM pg_catalog.pg_class c "
               "JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace WHERE n.nspname = " + literal(schema) +
               " AND c.relkind IN ('r', 'p', 'v', 'm', 'f', 'S') ORDER BY c.relname";
    case Columns:
        return "SELECT a.attname AS name, pg_catalog.format_type(a.atttypid, a.atttypmod) AS type, a.attnotnull AS not_null "
               "FROM pg_catalog.pg_attribute a JOIN pg_catalog.pg_class c ON c.oid = a.attrelid "
               "JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace WHERE n.nspname = " + literal(schema) +
               " AND c.relname = " + literal(relation) + " AND a.attnum > 0 AND NOT a.attisdropped ORDER BY a.attnum";
    case DatabaseProperties:
        return "SELECT datname AS name, pg_catalog.pg_get_userbyid(datdba) AS owner, "
               "pg_catalog.pg_encoding_to_char(encoding) AS encoding, datcollate AS collation, "
               "datconnlimit::text AS connection_limit, datallowconn::text AS allow_connections, "
               "pg_catalog.shobj_description(oid, 'pg_database') AS comment "
               "FROM pg_catalog.pg_database WHERE datname = pg_catalog.current_database()";
    case SchemaProperties:
        return "SELECT nspname AS name, pg_catalog.pg_get_userbyid(nspowner) AS owner, "
               "pg_catalog.obj_description(oid, 'pg_namespace') AS comment "
               "FROM pg_catalog.pg_namespace WHERE nspname = " + literal(schema);
    case RelationProperties:
        return "SELECT c.relname AS name, n.nspname AS schema, c.oid::text AS oid, "
               "CASE c.relkind WHEN 'r' THEN 'Table' WHEN 'p' THEN 'Partitioned table' WHEN 'v' THEN 'View' "
               "WHEN 'm' THEN 'Materialized view' WHEN 'S' THEN 'Sequence' WHEN 'f' THEN 'Foreign table' END AS type, "
               "pg_catalog.pg_get_userbyid(c.relowner) AS owner, c.relpersistence::text AS persistence, "
               "c.reltuples::text AS estimated_rows, c.relrowsecurity::text AS row_security, "
               "c.relispartition::text AS partition, pg_catalog.obj_description(c.oid, 'pg_class') AS comment "
               "FROM pg_catalog.pg_class c JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace "
               "WHERE n.nspname = " + literal(schema) + " AND c.relname = " + literal(relation);
    case ColumnProperties:
        return "SELECT a.attname AS name, n.nspname AS schema, c.relname AS relation, "
               "pg_catalog.format_type(a.atttypid, a.atttypmod) AS type, a.attnotnull::text AS not_null, "
               "a.attidentity::text AS identity, a.attgenerated::text AS generated, "
               "pg_catalog.pg_get_expr(d.adbin, d.adrelid) AS default_value, "
               "pg_catalog.col_description(c.oid, a.attnum) AS comment "
               "FROM pg_catalog.pg_attribute a JOIN pg_catalog.pg_class c ON c.oid = a.attrelid "
               "JOIN pg_catalog.pg_namespace n ON n.oid = c.relnamespace "
               "LEFT JOIN pg_catalog.pg_attrdef d ON d.adrelid = a.attrelid AND d.adnum = a.attnum "
               "WHERE n.nspname = " + literal(schema) + " AND c.relname = " + literal(relation) +
               " AND a.attname = " + literal(column) + " AND a.attnum > 0 AND NOT a.attisdropped";
    }
    return {};
}

// Queue a uniquely identified request on its database session, evicting only idle pool entries if needed.
quint64 CatalogService::request(const QVariantMap &profile, const QString &password, Kind kind,
                               const QString &schema, const QString &relation, const QString &column)
{
    const quint64 id = nextRequest++;
    const auto profileId = profile.value("serverId", profile.value("id")).toString();
    const auto key = QString::fromUtf8(QJsonDocument(QJsonArray{profileId, profile.value("database").toString()}).toJson(QJsonDocument::Compact));
    if (!connections.contains(key)) {
        if (connections.size() >= 8) {
            auto idle = connections.end();
            for (auto it = connections.begin(); it != connections.end(); ++it)
                if (it->requests.isEmpty()) { idle = it; break; }
            if (idle == connections.end()) {
                QTimer::singleShot(0, this, [this, id] { emit failed(id, tr("Alle metadataverbindingen zijn bezig. Probeer opnieuw via Vernieuwen.")); });
                return id;
            }
            auto *session = idle->session;
            disconnect(session, nullptr, this, nullptr);
            connections.erase(idle);
            session->deleteLater();
        }
        auto *session = new PgSession(this);
        connections.insert(key, {session, profileId, {}, {}, {}});
        const QPointer<PgSession> guard = session;
        connect(session, &PgSession::connected, this, [this, key, guard] {
            if (guard && connections.contains(key) && connections[key].session == guard) startNext(key);
        });
        connect(session, &PgSession::resultReady, this, [this, key, guard](const QueryResult &result) {
            if (!guard || !connections.contains(key) || connections[key].session != guard) return;
            auto &entry = connections[key];
            if (result.error) entry.error = result.status;
            if (result.truncated) entry.error = tr("De cataloguslijst is te groot; verfijn de selectie.");
            for (const auto &row : result.rows) {
                QVariantMap value;
                for (int col = 0; col < row.size(); ++col) value[result.columns.value(col)] = row[col].text;
                entry.rows.append(value);
            }
        });
        connect(session, &PgSession::executionFinished, this, [this, key, guard](bool success, const QString &) {
            if (!guard || !connections.contains(key) || connections[key].session != guard || connections[key].requests.isEmpty()) return;
            auto &entry = connections[key];
            const auto request = entry.requests.dequeue();
            const auto rows = entry.rows;
            const auto error = entry.error;
            entry.rows.clear();
            entry.error.clear();
            if (success && error.isEmpty()) emit loaded(request.id, rows);
            else emit failed(request.id, error.isEmpty() ? tr("Catalogus kon niet worden gelezen.") : error);
            QTimer::singleShot(0, this, [this, key] { startNext(key); });
        });
        connect(session, &PgSession::failed, this, [this, key, guard](const QString &message, bool) {
            if (!guard || !connections.contains(key) || connections[key].session != guard) return;
            auto requests = connections[key].requests;
            connections[key].requests.clear();
            for (const auto &request : requests) emit failed(request.id, message);
        });
        session->open(profile, password);
    }
    auto &entry = connections[key];
    entry.requests.enqueue({id, sql(kind, schema, relation, column)});
    if (entry.session->state() == PgSession::Disconnected) entry.session->open(profile, password);
    else startNext(key);
    return id;
}

// Dispatch the head request only once its metadata session is connected and idle.
void CatalogService::startNext(const QString &key)
{
    if (!connections.contains(key)) return;
    auto &entry = connections[key];
    if (entry.requests.isEmpty() || entry.session->isBusy() || entry.session->state() != PgSession::Connected) return;
    entry.rows.clear();
    entry.error.clear();
    entry.session->execute(entry.requests.head().sql);
}

// Invalidate and dispose of all metadata sessions belonging to one root profile.
void CatalogService::closeServer(const QString &id)
{
    for (auto it = connections.begin(); it != connections.end();) {
        if (it->profileId != id) { ++it; continue; }
        auto *session = it->session;
        disconnect(session, nullptr, this, nullptr);
        it = connections.erase(it);
        session->deleteLater();
    }
}

// Dispose of the selected database session synchronously so a following DROP can proceed.
void CatalogService::closeDatabase(const QString &profileId, const QString &database)
{
    const auto key = QString::fromUtf8(QJsonDocument(QJsonArray{profileId, database}).toJson(QJsonDocument::Compact));
    if (!connections.contains(key)) return;
    auto *session = connections.take(key).session;
    disconnect(session, nullptr, this, nullptr);
    delete session;
}
