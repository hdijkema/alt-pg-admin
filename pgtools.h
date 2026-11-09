#ifndef PGTOOLS_H
#define PGTOOLS_H
#include <QProcessEnvironment>
#include <QVariantMap>

namespace PgTools {
// goal: Locate a bundled PostgreSQL tool and supply explicit connection defaults.
// pre: Profiles use the same host/port/database/user/sslmode fields as PgSession.
// post: The process environment is a private copy; no global settings or files change.
// internals: Prefer tools beside the application; on non-Windows allow PATH lookup.
//            Remove inherited service/hostaddr/password overrides before applying the profile.
QString executable(const QString &tool);
QProcessEnvironment environment(const QVariantMap &profile, const QString &password);
// Return a libpq dbname connection string, preserving names containing '=' or quotes.
// The result contains no password and is safe as one QProcess argument, not shell text.
QString databaseArgument(QString database);
}
#endif
