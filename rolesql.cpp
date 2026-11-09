#include "rolesql.h"

namespace {
QString identifier(QString text) { text.replace('"', "\"\""); return '"' + text + '"'; }
QString literal(QString text) { text.replace('\\', "\\\\"); text.replace('\'', "''"); return "E'" + text + "'"; }
}

// Expose only public role attributes and membership names, never password columns.
QString RoleSql::rolesQuery()
{
    return "SELECT r.rolname AS name, r.rolcanlogin::text AS login, r.rolinherit::text AS inherit, "
        "r.rolcreatedb::text AS createdb, r.rolcreaterole::text AS createrole, r.rolsuper::text AS superuser, "
        "r.rolreplication::text AS replication, r.rolbypassrls::text AS bypassrls, r.rolconnlimit::text AS connection_limit, "
        "r.rolvaliduntil::text AS valid_until, pg_catalog.shobj_description(r.oid,'pg_authid') AS comment, "
        "COALESCE((SELECT pg_catalog.json_agg(p.rolname ORDER BY p.rolname) FROM pg_catalog.pg_auth_members m "
        "JOIN pg_catalog.pg_roles p ON p.oid=m.roleid WHERE m.member=r.oid),'[]')::text AS memberships "
        "FROM pg_catalog.pg_roles r ORDER BY r.rolname;";
}

// List explicit database targets independently of which database the role window connects to.
QString RoleSql::databasesQuery()
{
    return "SELECT datname AS name FROM pg_catalog.pg_database WHERE NOT datistemplate ORDER BY datname;";
}

// Separate direct ACL entries/grant options from effective rights, including PUBLIC/inheritance.
QString RoleSql::databaseRightsQuery(const QString &role, const QString &database)
{
    return "SELECT p.privilege, EXISTS(SELECT 1 FROM pg_catalog.aclexplode(COALESCE(d.datacl,pg_catalog.acldefault('d',d.datdba))) a "
        "WHERE a.grantee=r.oid AND a.privilege_type=p.privilege)::text AS direct, "
        "EXISTS(SELECT 1 FROM pg_catalog.aclexplode(COALESCE(d.datacl,pg_catalog.acldefault('d',d.datdba))) a "
        "WHERE a.grantee=r.oid AND a.privilege_type=p.privilege AND a.is_grantable)::text AS grantable, "
        "pg_catalog.has_database_privilege(r.oid,d.oid,p.privilege)::text AS effective, "
        "pg_catalog.pg_get_userbyid(d.datdba) AS owner FROM pg_catalog.pg_database d "
        "CROSS JOIN pg_catalog.pg_roles r CROSS JOIN (VALUES ('CONNECT',1),('CREATE',2),('TEMPORARY',3)) p(privilege,position) "
        "WHERE r.rolname=" + literal(role) + " AND d.datname=" + literal(database) + " ORDER BY p.position;";
}

// Protect reserved/system names and reject truncation before constructing any DDL.
QString RoleSql::nameError(const QString &name)
{
    if (name.trimmed().isEmpty()) return "Vul een rolnaam in.";
    if (name.toUtf8().size() > 63) return "De rolnaam is langer dan 63 UTF-8-bytes.";
    if (name.startsWith("pg_")) return "Systeemrollen met prefix pg_ kunnen hier niet worden gewijzigd.";
    for (const auto character : name)
        if (character.category() == QChar::Other_Control) return "De rolnaam bevat een stuurteken.";
    return {};
}

// Build only changed attributes/memberships; unchanged grant-option state stays untouched.
QString RoleSql::roleChange(const QVariantMap &desired, const QVariantMap &original, QString *error)
{
    const auto name = desired.value("name").toString();
    *error = nameError(name);
    if (!error->isEmpty()) return {};
    const bool create = original.isEmpty();
    if (!create && name != original.value("name").toString()) { *error = "Hernoemen van rollen wordt hier nog niet ondersteund."; return {}; }
    QStringList options;
    const QList<QPair<QString, QString>> attributes = {{"login","LOGIN"},{"inherit","INHERIT"},
        {"createdb","CREATEDB"},{"createrole","CREATEROLE"},{"superuser","SUPERUSER"},
        {"replication","REPLICATION"},{"bypassrls","BYPASSRLS"}};
    for (const auto &attribute : attributes) {
        const bool value = desired.value(attribute.first, attribute.first == "inherit").toBool();
        const bool before = original.value(attribute.first, attribute.first == "inherit").toBool();
        if (create ? value != (attribute.first == "inherit") : value != before)
            options.append((value ? QString() : "NO") + attribute.second);
    }
    const int limit = desired.value("connection_limit", -1).toInt();
    if (limit < -1) { *error = "Ongeldige verbindingslimiet."; return {}; }
    if (create ? limit != -1 : limit != original.value("connection_limit", -1).toInt())
        options.append("CONNECTION LIMIT " + QString::number(limit));
    const auto until = desired.value("valid_until").toString().trimmed();
    if (create ? !until.isEmpty() : until != original.value("valid_until").toString())
        options.append("VALID UNTIL " + literal(until.isEmpty() ? QString("infinity") : until));
    if (desired.value("clear_password").toBool()) options.append("PASSWORD NULL");
    QStringList statements;
    if (create || !options.isEmpty()) statements.append((create ? "CREATE ROLE " : "ALTER ROLE ") +
        identifier(name) + (options.isEmpty() ? QString() : " WITH " + options.join(' ')) + ';');
    const auto comment = desired.value("comment").toString();
    if (create ? !comment.isEmpty() : comment != original.value("comment").toString())
        statements.append("COMMENT ON ROLE " + identifier(name) + " IS " + (comment.isEmpty() ? "NULL" : literal(comment)) + ';');
    const auto parents = desired.value("memberships").toStringList();
    const auto previous = original.value("memberships").toStringList();
    for (const auto &parent : parents) {
        if (parent == name) { *error = "Een rol kan geen lid zijn van zichzelf."; return {}; }
        if (!previous.contains(parent)) statements.append("GRANT " + identifier(parent) + " TO " + identifier(name) + ';');
    }
    for (const auto &parent : previous)
        if (!parents.contains(parent)) statements.append("REVOKE " + identifier(parent) + " FROM " + identifier(name) + ';');
    return statements.join('\n');
}

// Emit direct database grants/revokes and an optional, explicitly requested owner change.
QString RoleSql::databaseChange(const QString &role, const QString &database, const QVariantMap &desired,
                                const QVariantMap &original, bool makeOwner, QString *error)
{
    *error = nameError(role);
    if (!error->isEmpty()) return {};
    if (database.isEmpty() || database.contains(QChar(0))) { *error = "Kies een database."; return {}; }
    QStringList statements;
    for (const auto &privilege : {QString("CONNECT"), QString("CREATE"), QString("TEMPORARY")}) {
        const bool value = desired.value(privilege).toBool();
        const bool before = original.value(privilege).toBool();
        const bool grant = value && desired.value(privilege + "_grant").toBool();
        const bool oldGrant = original.value(privilege + "_grant").toBool();
        const auto target = privilege + " ON DATABASE " + identifier(database);
        if (value && (!before || (grant && !oldGrant)))
            statements.append("GRANT " + target + " TO " + identifier(role) + (grant ? " WITH GRANT OPTION;" : ";"));
        else if (!value && before) statements.append("REVOKE " + target + " FROM " + identifier(role) + ';');
        if (value && before && oldGrant && !grant)
            statements.append("REVOKE GRANT OPTION FOR " + target + " FROM " + identifier(role) + ';');
    }
    if (makeOwner && original.value("owner").toString() != role)
        statements.append("ALTER DATABASE " + identifier(database) + " OWNER TO " + identifier(role) + ';');
    return statements.join('\n');
}
