#include "pgtools.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

// Resolve one concrete tool; an empty result lets the caller report a missing executable.
QString PgTools::executable(const QString &tool)
{
    QString path = QCoreApplication::applicationDirPath() + "/" + tool;
#ifdef Q_OS_WIN
    path += ".exe";
#else
    if (!QFileInfo::exists(path)) path = QStandardPaths::findExecutable(tool);
#endif
    return QFileInfo::exists(path) ? path : QString();
}

// Reuse SSL/certificate/password-file conventions without inheriting a different server login.
QProcessEnvironment PgTools::environment(const QVariantMap &profile, const QString &password)
{
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.remove("PGHOSTADDR");
    environment.remove("PGSERVICE");
    environment.remove("PGPASSWORD");
    environment.insert("PGHOST", profile.value("host").toString());
    environment.insert("PGPORT", profile.value("port", 5432).toString());
    environment.insert("PGDATABASE", profile.value("database").toString());
    environment.insert("PGUSER", profile.value("user").toString());
    environment.insert("PGSSLMODE", profile.value("sslmode", "prefer").toString());
    environment.insert("PGCONNECT_TIMEOUT", QString::number(qBound(2, profile.value("timeout", 10).toInt(), 120)));
    environment.insert("PGCLIENTENCODING", "UTF8");
    environment.insert("PGAPPNAME", "pgadmin-jr tools");
    if (!password.isEmpty()) environment.insert("PGPASSWORD", password);
    return environment;
}

// Quote libpq keyword values rather than relying on filename/identifier quoting rules.
QString PgTools::databaseArgument(QString database)
{
    database.replace('\\', "\\\\");
    database.replace('\'', "\\'");
    return "dbname='" + database + "'";
}
