#include "databasejob.h"
#include "database/pgtools.h"
#include "database/pgsession.h"
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QtConcurrentRun>

// No connection or subprocess is created until a concrete operation is requested.
DatabaseJob::DatabaseJob(QObject *parent) : QObject(parent), cancelled(std::make_shared<std::atomic_bool>(false)) {}

// Stop work before deleting its temporary file; the copying worker never accesses this QObject.
DatabaseJob::~DatabaseJob()
{
    cancelled->store(true);
    if (process && process->state() != QProcess::NotRunning) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(1000);
    }
    copyFuture.waitForFinished();
    delete maintenance;
    delete temporary;
}

// Validate ordinary PostgreSQL names before any server operation or subprocess starts.
QString DatabaseJob::nameError(const QString &database)
{
    if (database.trimmed().isEmpty()) return tr("Vul een databasenaam in.");
    if (database.toUtf8().size() > 63) return tr("De databasenaam is langer dan 63 UTF-8-bytes.");
    for (const auto character : database)
        if (character.category() == QChar::Other_Control) return tr("De databasenaam bevat een stuurteken.");
    return {};
}

// Keep the connection's maintenance database and the standard system databases protected.
QString DatabaseJob::dropError(const QVariantMap &profile, const QString &database)
{
    const auto error = nameError(database);
    if (!error.isEmpty()) return error;
    if (database == "postgres" || database == "template0" || database == "template1")
        return tr("Deze systeemdatabase kan niet via dit menu worden verwijderd.");
    if (database == profile.value("database").toString())
        return tr("Dit is de onderhoudsdatabase van het profiel. Kies eerst een andere onderhoudsdatabase.");
    return {};
}

// Detect the PostgreSQL custom header or standard tar header without loading the archive.
bool DatabaseJob::isArchive(const QString &filename)
{
    if (filename.isEmpty()) return false;
    QFile file(filename);
    if (!file.open(QIODevice::ReadOnly)) return false;
    const auto header = file.read(512);
    return header.startsWith("PGDMP") || header.mid(257, 5) == "ustar";
}

// Stage the entire dump beside its destination; an existing backup is never used as tool output.
void DatabaseJob::backup(const QVariantMap &profile, const QString &password, const QString &database,
                         const QString &filename, bool plainSql)
{
    if (running) return;
    running = true;
    operation = Backup;
    cancelled = std::make_shared<std::atomic_bool>(false);
    const auto error = nameError(database);
    if (!error.isEmpty()) { complete(false, error); return; }
    if (filename.isEmpty()) { complete(false, tr("Kies een backupbestand.")); return; }
    destination = QFileInfo(filename).absoluteFilePath();
    delete temporary;
    temporary = new QTemporaryFile(QFileInfo(destination).absolutePath() + "/.pgadm-backup-XXXXXX");
    if (!temporary->open()) { complete(false, temporary->errorString()); return; }
    const auto staging = temporary->fileName();
    temporary->close();
    launch("pg_dump", profile, password, {"--no-password", "--verbose", plainSql ? "--format=p" : "--format=c",
        "--encoding=UTF8", "--dbname=" + PgTools::databaseArgument(database), "--file=" + staging});
}

// Let PostgreSQL restore a single transaction and stop at its first error, without --create.
void DatabaseJob::restore(const QVariantMap &profile, const QString &password, const QString &database,
                          const QString &filename, bool clean, bool skipOwners)
{
    if (running) return;
    running = true;
    operation = Restore;
    cancelled = std::make_shared<std::atomic_bool>(false);
    const auto error = nameError(database);
    if (!error.isEmpty()) { complete(false, error); return; }
    if (!QFileInfo(filename).isFile()) { complete(false, tr("Het backupbestand bestaat niet.")); return; }
    if (isArchive(filename)) {
        QStringList arguments{"--no-password", "--verbose", "--single-transaction", "--exit-on-error",
            "--dbname=" + PgTools::databaseArgument(database)};
        if (clean) arguments << "--clean" << "--if-exists";
        if (skipOwners) arguments << "--no-owner" << "--no-privileges";
        arguments << "--" << QFileInfo(filename).absoluteFilePath();
        launch("pg_restore", profile, password, arguments);
    } else {
        if (clean) { complete(false, tr("Objecten vervangen is alleen beschikbaar voor PostgreSQL-archieven.")); return; }
        launch("psql", profile, password, {"--no-psqlrc", "--no-password", "--single-transaction",
            "--set=ON_ERROR_STOP=on", "--dbname=" + PgTools::databaseArgument(database),
            "--file=" + QFileInfo(filename).absoluteFilePath()});
    }
}

// createdb quotes the new identifier and connects to an explicit, separate maintenance database.
void DatabaseJob::create(const QVariantMap &profile, const QString &password, const QString &database)
{
    if (running) return;
    running = true;
    operation = Create;
    cancelled = std::make_shared<std::atomic_bool>(false);
    const auto error = nameError(database);
    if (!error.isEmpty()) { complete(false, error); return; }
    launch("createdb", profile, password, {"--no-password", "--encoding=UTF8", "--template=template0",
        "--maintenance-db=" + PgTools::databaseArgument(profile.value("database", "postgres").toString()), "--", database});
}

// Never force other clients off a database; PostgreSQL reports active connections and missing rights.
void DatabaseJob::drop(const QVariantMap &profile, const QString &password, const QString &database)
{
    if (running) return;
    running = true;
    operation = Drop;
    cancelled = std::make_shared<std::atomic_bool>(false);
    const auto error = dropError(profile, database);
    if (!error.isEmpty()) { complete(false, error); return; }
    launch("dropdb", profile, password, {"--no-password",
        "--maintenance-db=" + PgTools::databaseArgument(profile.value("database", "postgres").toString()), "--", database});
}

// Quote identifiers independently; no server-provided name is treated as an SQL fragment.
QString DatabaseJob::maintenanceSql(Operation operation, const QString &database, const QString &schema,
                                    const QString &relation, bool vacuumAnalyze, bool reindexConcurrently, QString *error)
{
    *error = nameError(database);
    if (!error->isEmpty()) return {};
    if (operation != Vacuum && operation != Analyze && operation != Reindex) { *error = tr("Kies een onderhoudsactie."); return {}; }
    if (schema.isEmpty() != relation.isEmpty() || (!schema.isEmpty() &&
        (!nameError(schema).isEmpty() || !nameError(relation).isEmpty()))) {
        *error = tr("Ongeldig schema of ongeldige tabelnaam."); return {};
    }
    auto identifier = [](QString name) { name.replace('"', "\"\""); return '"' + name + '"'; };
    const auto target = relation.isEmpty() ? QString() : identifier(schema) + '.' + identifier(relation);
    if (operation == Vacuum) return "VACUUM (VERBOSE" + QString(vacuumAnalyze ? ", ANALYZE" : "") + ')' +
        (target.isEmpty() ? QString() : ' ' + target) + ';';
    if (operation == Analyze) return "ANALYZE (VERBOSE)" + (target.isEmpty() ? QString() : ' ' + target) + ';';
    return "REINDEX (VERBOSE) " + QString(target.isEmpty() ? "DATABASE " : "TABLE ") +
        (reindexConcurrently ? "CONCURRENTLY " : "") + (target.isEmpty() ? identifier(database) : target) + ';';
}

// Use the existing asynchronous libpq worker outside every query-tab transaction.
void DatabaseJob::maintain(Operation requested, const QVariantMap &profile, const QString &password,
                           const QString &database, const QString &schema, const QString &relation,
                           bool vacuumAnalyze, bool reindexConcurrently)
{
    if (running) return;
    running = true;
    operation = requested;
    cancelled = std::make_shared<std::atomic_bool>(false);
    QString error;
    const auto sql = maintenanceSql(requested, database, schema, relation, vacuumAnalyze, reindexConcurrently, &error);
    if (!error.isEmpty()) { complete(false, error); return; }
    auto runtime = profile;
    runtime["database"] = database;
    maintenance = new PgSession(this);
    connect(maintenance, &PgSession::connected, this, [this, sql] {
        if (!cancelled->load()) maintenance->execute(sql);
        else maintenance->close();
    });
    connect(maintenance, &PgSession::notice, this, [this](const QString &message) { emit output(message + '\n'); });
    connect(maintenance, &PgSession::resultReady, this, [this](const QueryResult &result) {
        if (!result.status.isEmpty()) emit output(result.status + '\n');
    });
    connect(maintenance, &PgSession::executionFinished, this, [this](bool success, const QString &) {
        const bool stopped = cancelled->load();
        complete(success && !stopped, stopped ? tr("Onderhoud geannuleerd. Reeds afgeronde onderdelen blijven van kracht; zie de loguitvoer.") :
            success ? tr("Onderhoud voltooid; zie ook eventuele waarschuwingen in de loguitvoer.") : tr("Onderhoud mislukt. Zie de loguitvoer."));
    });
    connect(maintenance, &PgSession::failed, this, [this](const QString &message, bool) { complete(false, message); });
    connect(maintenance, &PgSession::disconnected, this, [this] {
        if (running) complete(false, tr("Onderhoudsverbinding gesloten."));
    });
    emit output(sql + '\n');
    maintenance->open(runtime, password);
}

// Run one tool without a shell; release the process environment as soon as the tool finishes.
void DatabaseJob::launch(const QString &tool, const QVariantMap &profile, const QString &password, const QStringList &arguments)
{
    const auto executable = PgTools::executable(tool);
    if (executable.isEmpty()) { complete(false, tr("%1 ontbreekt naast de applicatie. Bouw opnieuw met de PostgreSQL 18-tools.").arg(tool)); return; }
    decoder.resetState();
    process = new QProcess(this);
    process->setProcessChannelMode(QProcess::MergedChannels);
    process->setProcessEnvironment(PgTools::environment(profile, password));
    connect(process, &QProcess::readyReadStandardOutput, this, &DatabaseJob::readOutput);
    connect(process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) complete(false, tr("Het PostgreSQL-hulpmiddel kon niet starten: %1").arg(process->errorString()));
    });
    connect(process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
        readOutput();
        if (cancelled->load()) {
            QString message = tr("Taak geannuleerd.");
            if (operation == Backup) message = tr("Backup geannuleerd; het vorige bestand is behouden.");
            if (operation == Create || operation == Drop)
                message = tr("Geannuleerd. Controleer de databaseboom; CREATE/DROP kan al zijn voltooid.");
            complete(false, message);
        } else if (exitCode != 0 || status != QProcess::NormalExit) {
            complete(false, tr("Taak mislukt (code %1). Zie de loguitvoer. Controleer bij ontbrekende uitvoer de PostgreSQL-runtimebestanden.").arg(exitCode));
        } else if (operation == Backup) {
            process->deleteLater();
            process = nullptr;
            commitBackup();
        } else complete(true, tr("Taak voltooid."));
    });
    emit output(tr("PostgreSQL-taak gestart.\n"));
    process->start(executable, arguments);
}

// Decode split UTF-8 log chunks incrementally; binary backups are written directly to staging.
void DatabaseJob::readOutput()
{
    if (!process) return;
    const auto bytes = process->readAllStandardOutput();
    if (!bytes.isEmpty()) emit output(decoder(bytes));
}

// Copy/commit on a worker so even very large backups keep the event loop available.
void DatabaseJob::commitBackup()
{
    emit output(tr("Backup wordt afgerond...\n"));
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher] {
        const auto error = watcher->result();
        watcher->deleteLater();
        complete(error.isEmpty(), error.isEmpty() ? tr("Backup opgeslagen: %1").arg(destination) : error);
    });
    copyFuture = QtConcurrent::run([source = temporary->fileName(), target = destination, flag = cancelled] {
        QFile input(source);
        QSaveFile output(target);
        if (!input.open(QIODevice::ReadOnly)) return input.errorString();
        if (!output.open(QIODevice::WriteOnly)) return output.errorString();
        while (!input.atEnd()) {
            if (flag->load()) return QString("Backup geannuleerd; het vorige bestand is behouden.");
            const auto bytes = input.read(1024 * 1024);
            if (input.error() != QFileDevice::NoError) return input.errorString();
            if (output.write(bytes) != bytes.size()) return output.errorString();
        }
        if (flag->load()) return QString("Backup geannuleerd; het vorige bestand is behouden.");
        return output.commit() ? QString() : output.errorString();
    });
    watcher->setFuture(copyFuture);
}

// Clean resources before reporting completion, including failures before a process was started.
void DatabaseJob::complete(bool success, const QString &message)
{
    if (maintenance) {
        maintenance->disconnect(this);
        maintenance->close();
        maintenance->deleteLater();
        maintenance = nullptr;
    }
    if (process) {
        process->disconnect(this);
        process->deleteLater();
        process = nullptr;
    }
    delete temporary;
    temporary = nullptr;
    running = false;
    emit output(message + '\n');
    emit finished(success, message);
}

// A cancelled backup never commits; restore rollback is handled by the server after disconnect.
void DatabaseJob::cancel()
{
    if (!running) return;
    cancelled->store(true);
    if (maintenance) {
        if (maintenance->isBusy()) maintenance->cancel();
        else maintenance->close();
    }
    if (process) process->kill();
}
