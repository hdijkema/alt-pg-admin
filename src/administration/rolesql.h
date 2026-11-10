#ifndef ROLESQL_H
#define ROLESQL_H
#include <QVariantMap>

namespace RoleSql {
// goal: Build concrete, quoted PostgreSQL role and database-privilege commands.
// pre: error is non-null. Names are literal role/database names, not SQL fragments.
// post: No SQL is executed here; passwords/verifiers never appear in these maps or SQL.
// internals: Read pg_roles/pg_auth_members (no password columns), compare desired
//            fields with their loaded baseline and emit only actual changes.
QString rolesQuery();
QString databasesQuery();
QString databaseRightsQuery(const QString &role, const QString &database);
// Empty original means CREATE. Existing names cannot be renamed by this editor.
// memberships is QStringList; clear_password is an explicit PASSWORD NULL request.
QString roleChange(const QVariantMap &desired, const QVariantMap &original, QString *error);
// Maps use CONNECT/CREATE/TEMPORARY and *_grant keys. Changing direct grants does
// not revoke rights inherited through PUBLIC, ownership or another role.
QString databaseChange(const QString &role, const QString &database, const QVariantMap &desired,
                       const QVariantMap &original, bool makeOwner, QString *error);
QString nameError(const QString &name);
}
#endif
