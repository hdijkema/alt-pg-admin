#include "pgsession.h"
#include "sqlscript.h"
#include "results/fullresultexport.h"

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QHostAddress>
#include <QHostInfo>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QTimer>
#include <libpq-fe.h>
#include <algorithm>

class PgConnection : public QObject
{
    Q_OBJECT
public:
    // Create the worker-owned connection timeout; no database is opened by construction.
    explicit PgConnection(const std::shared_ptr<std::atomic_bool> &cancelled)
        : timeout(new QTimer(this)), exportCancelled(cancelled)
    {
        timeout->setSingleShot(true);
        connect(timeout, &QTimer::timeout, this, [this] {
            fail(tr("Verbindingstime-out. Controleer host, poort en netwerk."), false);
        });
    }
    ~PgConnection() override { release(); }

    // Reset previous resources and resolve DNS asynchronously within the overall connection deadline.
    void open(const QVariantMap &profile, const QString &password)
    {
        release();
        elapsed.start();
        timeout->start(qBound(2, profile.value("timeout", 10).toInt(), 120) * 1000);
        const auto host = profile.value("host").toString();
        if (!QHostAddress(host).isNull() || host.startsWith('/')) {
            startConnection(profile, password);
            return;
        }
        // Resolve names asynchronously so cancel/timeout and shutdown also work during DNS.
        lookup = QHostInfo::lookupHost(host, this, [this, profile, password](const QHostInfo &info) {
            lookup = -1;
            if (info.error() != QHostInfo::NoError || info.addresses().isEmpty()) {
                fail(tr("Hostnaam kon niet worden opgezocht: %1").arg(info.errorString()), false);
                return;
            }
            auto resolved = profile;
            QStringList addresses;
            QStringList hosts;
            for (const auto &address : info.addresses()) {
                addresses.append(address.toString());
                hosts.append(profile.value("host").toString());
            }
            resolved["host"] = hosts.join(',');
            resolved["hostaddr"] = addresses.join(',');
            startConnection(resolved, password);
        });
    }

    // Pass separate libpq parameters without connection-string expansion and begin nonblocking polling.
    void startConnection(const QVariantMap &profile, const QString &password)
    {
        const auto host = profile.value("host").toString().toUtf8();
        const auto hostaddr = profile.value("hostaddr").toString().toUtf8();
        const auto port = profile.value("port", 5432).toString().toUtf8();
        const auto database = profile.value("database").toString().toUtf8();
        const auto user = profile.value("user").toString().toUtf8();
        const auto ssl = profile.value("sslmode", "prefer").toString().toUtf8();
        auto secret = password.toUtf8();
        const char *keys[] = {"host", "hostaddr", "port", "dbname", "user", "password", "sslmode",
                              "application_name", "client_encoding", nullptr};
        const char *values[] = {host.constData(), hostaddr.constData(), port.constData(), database.constData(), user.constData(),
                                password.isEmpty() ? nullptr : secret.constData(), ssl.constData(), "PgAdmQt", "UTF8", nullptr};
        // expand_dbname=0: a database name containing '=' is never a connection string.
        connection = PQconnectStartParams(keys, values, 0);
        secret.fill('\0');
        if (!connection) {
            fail(tr("Onvoldoende geheugen voor een PostgreSQL-verbinding."), false);
            return;
        }
        if (PQstatus(connection) == CONNECTION_BAD) {
            connectionFailed();
            return;
        }
        watch(QSocketNotifier::Write);
    }

    // Finish any active script before releasing libpq and announcing disconnection.
    void close()
    {
        // Closing the connection itself rolls back; do not start asynchronous cleanup here.
        planGuardActive = false;
        if (running) finishScript(false);
        release();
        emit disconnected();
    }

    // Validate script splitting/string mode and initialise bounded sequential execution state.
    void execute(const QString &sql, bool rollbackExplain = false)
    {
        executeScript(sql, {}, false, {}, rollbackExplain);
    }

    // Keep the full-file path on the same worker/session as ordinary tab execution.
    void executeToFile(const QString &sql, const QString &path, bool excel, const QVector<int> &widths, bool rollbackExplain)
    {
        executeScript(sql, path, excel, widths, rollbackExplain);
    }

    // Initialise file staging before dispatch; preview storage keeps its original limits.
    void executeScript(const QString &sql, const QString &path, bool excel, const QVector<int> &widths, bool rollbackExplain)
    {
        if (!established || running) return;
        QString error;
// Scan statement boundaries outside quoted strings/bodies and nested comments, retaining result annotations.
        statements = SqlScript::split(sql, &error);
        if (rollbackExplain) {
            for (const auto &statement : statements)
                if (statement.keyword != "EXPLAIN") error = tr("Uitvoeringsplan accepteert alleen EXPLAIN-opdrachten.");
            const auto transaction = PQtransactionStatus(connection);
            if (transaction != PQTRANS_IDLE && transaction != PQTRANS_INTRANS)
                error = tr("Herstel eerst de open transactie met ROLLBACK voordat je een uitvoeringsplan opvraagt.");
        }
        const auto *strings = PQparameterStatus(connection, "standard_conforming_strings");
        if (strings && QByteArray(strings) != "on") {
            static const QRegularExpression reset("^\\s*(?:RESET\\s+standard_conforming_strings|SET\\s+standard_conforming_strings\\s*(?:=|TO)\\s*(?:on|true|1))\\s*;?\\s*$", QRegularExpression::CaseInsensitiveOption);
            if (!reset.match(sql).hasMatch()) error = tr("Zet eerst standard_conforming_strings aan: SET standard_conforming_strings = on;");
        }
        if (statements.size() > 2000) error = tr("Maximaal 2000 opdrachten per uitvoering.");
        running = true;
        statementIndex = -1;
        keptBytes = 0;
        keptRows = 0;
        cancelled = false;
        exportPath = path;
        exportRows = 0;
        exportReported = false;
        exportClock.start();
        if (!path.isEmpty()) {
            fileExport = std::make_unique<FullResultExport>(path, excel, widths, statements.size() > 1);
            if (!fileExport->errorString().isEmpty()) error = fileExport->errorString();
            if (exportCancelled->load()) error = tr("Export geannuleerd voordat SQL werd uitgevoerd.");
        }
        if (!error.isEmpty() || statements.isEmpty()) {
            QueryResult result;
            result.number = 1;
            result.title = tr("Script");
            result.finished = true;
            result.error = !error.isEmpty();
            result.status = error.isEmpty() ? tr("Geen SQL-opdrachten gevonden.") : error;
            emit resultReady(result);
            finishScript(error.isEmpty());
            return;
        }
        if (rollbackExplain) {
            const bool savepoint = PQtransactionStatus(connection) == PQTRANS_INTRANS;
            planCleanup = savepoint ? "ROLLBACK TO SAVEPOINT pgadm_explain; RELEASE SAVEPOINT pgadm_explain;" : "ROLLBACK;";
            planStarting = true;
            sendPlanControl(savepoint ? "SAVEPOINT pgadm_explain;" : "BEGIN;");
        } else nextStatement();
    }

    // Generate SCRAM locally with an explicit algorithm, avoiding a blocking
    // password_encryption lookup and plaintext in server statement logs.
    void changeRole(const QString &sql, const QString &role, const QString &password)
    {
        if (!established || running) return;
        auto secret = password.toUtf8();
        const auto username = role.toUtf8();
        auto *verifier = PQencryptPasswordConn(connection, secret.constData(), username.constData(), "scram-sha-256");
        secret.fill('\0');
        if (!verifier) {
            emit notice(tr("Het rolwachtwoord kon niet veilig worden voorbereid."));
            emit executionFinished(false, tr("Geen wijzigingen uitgevoerd"));
            return;
        }
        QString identifier = role;
        identifier.replace('"', "\"\"");
        const auto command = "BEGIN;\n" + sql + "\nALTER ROLE \"" + identifier + "\" PASSWORD '" +
            QString::fromLatin1(verifier) + "';\nCOMMIT;";
        PQfreemem(verifier);
        execute(command);
    }

    // Resume draining only after the GUI facade has delivered the previous batch.
    void acknowledge()
    {
        if (!waiting) return;
        waiting = false;
        if (running) QTimer::singleShot(0, this, &PgConnection::readResults);
    }

    // Request cancellation without blocking the connection worker.
    void cancel()
    {
        if (!running || cancelled || (planControl && !planStarting)) return;
        cancelled = true;
        if (planControl) return;
#ifdef LIBPQ_HAS_ASYNC_CANCEL
        cancelConnection = PQcancelCreate(connection);
        if (!cancelConnection || !PQcancelStart(cancelConnection)) {
            emit notice(tr("Annuleren kon niet worden gestart; verbreek zo nodig de verbinding."));
            clearCancel();
            return;
        }
        cancelElapsed.start();
        cancelPollStatus = PGRES_POLLING_WRITING;
        pollCancel();
#else
        // Older libpq cancellation blocks. Give a separate task its own handle,
        // keeping both the UI and the connection worker responsive.
        const auto handle = std::shared_ptr<PGcancel>(PQgetCancel(connection), PQfreeCancel);
        if (!handle) {
            emit notice(tr("Annuleren kon niet worden gestart; verbreek zo nodig de verbinding."));
            return;
        }
        auto *watcher = new QFutureWatcher<bool>(this);
        cancelTask = watcher;
        connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher] {
            emit notice(watcher->result()
                ? tr("Annuleringsverzoek verzonden; wachten op PostgreSQL.")
                : tr("Annuleren niet bevestigd; verbreek zo nodig de verbinding."));
            cancelTask = nullptr;
            watcher->deleteLater();
        });
        watcher->setFuture(QtConcurrent::run([handle] {
            char error[256] = {};
            return PQcancel(handle.get(), error, sizeof(error)) != 0;
        }));
#endif
    }

signals:
    void connected(const QVariantMap &information);
    void failed(const QString &message, bool needsPassword);
    void disconnected();
    void resultReady(const QueryResult &result);
    void notice(const QString &message);
    void executionFinished(bool success, const QString &transactionStatus);
    void transactionChanged(bool active);
    void exportProgress(qint64 rows);
    void exportFinished(bool success, const QString &path, qint64 rows, const QString &message);

private:
    // Stop DNS/timers/notifiers and free libpq handles, leaving worker state disconnected.
    void release()
    {
        fileExport.reset();
        planGuardActive = false;
        planControl = false;
        planStarting = false;
        clearCancel();
        timeout->stop();
        if (lookup >= 0) QHostInfo::abortHostLookup(lookup);
        lookup = -1;
        delete notifier;
        notifier = nullptr;
        if (connection) PQfinish(connection);
        connection = nullptr;
        established = false;
        running = false;
        waiting = false;
    }

    // Publish a terminal query error when needed, then release resources and report connection failure.
    void fail(const QString &message, bool needsPassword)
    {
        planGuardActive = false;
        if (running) {
            pending.error = true;
            pending.finished = true;
            pending.status = message;
            emit resultReady(pending);
            finishScript(false);
        }
        release();
        emit failed(message, needsPassword);
    }

    // Translate the libpq error and password-needed flag before releasing the handle.
    void connectionFailed()
    {
        const QString message = QString::fromUtf8(PQerrorMessage(connection)).trimmed();
        const bool needsPassword = PQconnectionNeedsPassword(connection) != 0;
        fail(message, needsPassword);
    }

    // Replace the socket notifier for the next libpq read/write readiness transition.
    void watch(QSocketNotifier::Type type)
    {
        if (notifier) {
            notifier->setEnabled(false);
            notifier->deleteLater();
            notifier = nullptr;
        }
        const int socket = PQsocket(connection);
        if (socket < 0) {
            connectionFailed();
            return;
        }
        notifier = new QSocketNotifier(socket, type, this);
        connect(notifier, &QSocketNotifier::activated, this, [this] {
            notifier->setEnabled(false);
            advance();
        });
    }

    // Poll connection progress or consume established input, keeping blocking network work out of the GUI.
    void advance()
    {
        if (established) {
            if (PQconsumeInput(connection) == 0) {
                connectionFailed();
                return;
            }
            // Drain notifications even though no LISTEN queries are issued yet.
            while (auto *notification = PQnotifies(connection)) PQfreemem(notification);
            if (running) readResults();
            else notifier->setEnabled(true);
            return;
        }
        const auto status = PQconnectPoll(connection);
        switch (status) {
        case PGRES_POLLING_READING: watch(QSocketNotifier::Read); break;
        case PGRES_POLLING_WRITING: watch(QSocketNotifier::Write); break;
        case PGRES_POLLING_OK: {
            timeout->stop();
            established = true;
            PQsetnonblocking(connection, 1);
            PQsetNoticeProcessor(connection, [](void *context, const char *message) {
                emit static_cast<PgConnection *>(context)->notice(QString::fromUtf8(message).trimmed());
            }, this);
            QVariantMap info;
            info["host"] = QString::fromUtf8(PQhost(connection));
            info["port"] = QString::fromUtf8(PQport(connection));
            info["database"] = QString::fromUtf8(PQdb(connection));
            info["user"] = QString::fromUtf8(PQuser(connection));
            info["serverVersion"] = QString::fromUtf8(PQparameterStatus(connection, "server_version"));
            info["ssl"] = PQsslInUse(connection) != 0;
            info["backendPid"] = PQbackendPID(connection);
            info["clientVersion"] = PQlibVersion();
            info["elapsedMs"] = elapsed.elapsed();
            watch(QSocketNotifier::Read);
            emit connected(info);
            break;
        }
        case PGRES_POLLING_FAILED: connectionFailed(); break;
        default:
            QTimer::singleShot(0, this, [this] { if (connection) advance(); });
            break;
        }
    }

    // Free the independent cancellation handle without closing the query connection.
    void clearCancel()
    {
#ifdef LIBPQ_HAS_ASYNC_CANCEL
        if (cancelConnection) PQcancelFinish(cancelConnection);
        cancelConnection = nullptr;
#else
        if (cancelTask) {
            cancelTask->disconnect(this);
            cancelTask->deleteLater();
            cancelTask = nullptr;
        }
#endif
    }

#ifdef LIBPQ_HAS_ASYNC_CANCEL
    // Poll cancellation readiness with short timers and a bounded confirmation deadline.
    void pollCancel()
    {
        if (!cancelConnection) return;
        const int socket = PQcancelSocket(cancelConnection);
        if (socket >= 0 && cancelElapsed.elapsed() <= 5000 && PQsocketPoll(socket,
            cancelPollStatus == PGRES_POLLING_READING, cancelPollStatus == PGRES_POLLING_WRITING, 0) == 0) {
            QTimer::singleShot(20, this, &PgConnection::pollCancel);
            return;
        }
        const auto status = PQcancelPoll(cancelConnection);
        cancelPollStatus = status;
        if (status == PGRES_POLLING_OK) {
            clearCancel();
            emit notice(tr("Annuleringsverzoek verzonden; wachten op PostgreSQL."));
        } else if (status == PGRES_POLLING_FAILED || cancelElapsed.elapsed() > 5000) {
            emit notice(tr("Annuleren niet bevestigd; verbreek zo nodig de verbinding."));
            clearCancel();
        } else QTimer::singleShot(20, this, &PgConnection::pollCancel);
    }
#endif

    // End execution, release cancellation state and report PostgreSQL transaction status.
    void finishScript(bool success)
    {
#ifndef LIBPQ_HAS_ASYNC_CANCEL
        // Do not allow another script on this session while its cancellation
        // task could still deliver a request to the same server backend.
        if (cancelTask && !cancelTask->isFinished()) {
            QTimer::singleShot(20, this, [this, success] {
                if (running) finishScript(success);
            });
            return;
        }
#endif
        if (planGuardActive && connection && established && PQstatus(connection) == CONNECTION_OK) {
#ifdef LIBPQ_HAS_ASYNC_CANCEL
            if (cancelConnection) {
                QTimer::singleShot(20, this, [this, success] { if (running) finishScript(success); });
                return;
            }
#endif
            planSuccess = success && !cancelled;
            sendPlanControl(planCleanup);
            return;
        }
        running = false;
        waiting = false;
        clearCancel();
        if (fileExport) {
            emit exportProgress(exportRows);
            const auto error = fileExport->finish(success && !cancelled, exportCancelled.get());
            fileExport.reset();
            success = success && !cancelled && error.isEmpty();
            emit exportFinished(success, exportPath, exportRows, error);
        }
        QString transaction = tr("Niet verbonden");
        if (connection) {
            switch (PQtransactionStatus(connection)) {
            case PQTRANS_IDLE: transaction = tr("Autocommit / geen transactie"); break;
            case PQTRANS_INTRANS: transaction = tr("Transactie open"); break;
            case PQTRANS_INERROR: transaction = tr("Transactie mislukt — ROLLBACK nodig"); break;
            default: transaction = tr("Transactiestatus onbekend"); break;
            }
        }
        const auto state = connection ? PQtransactionStatus(connection) : PQTRANS_UNKNOWN;
        emit transactionChanged(state == PQTRANS_INTRANS || state == PQTRANS_INERROR);
        emit executionFinished(success, transaction);
        if (notifier) notifier->setEnabled(true);
    }

    // Run BEGIN/SAVEPOINT or rollback cleanup asynchronously without publishing internal command rows.
    void sendPlanControl(const QString &sql)
    {
        planControl = true;
        queryFailed = false;
        waiting = false;
        if (notifier) notifier->setEnabled(false);
        if (!PQsendQuery(connection, sql.toUtf8().constData())) {
            planGuardActive = false;
            connectionFailed();
            return;
        }
        flushQuery();
    }

    // Drain transaction-control responses before dispatching plans or announcing final completion.
    void readPlanControl()
    {
        if (!PQconsumeInput(connection)) { connectionFailed(); return; }
        while (!PQisBusy(connection)) {
            auto *result = PQgetResult(connection);
            if (!result) {
                planControl = false;
                if (planStarting) {
                    planStarting = false;
                    planGuardActive = !queryFailed;
                    if (queryFailed) finishScript(false);
                    else nextStatement();
                } else {
                    planGuardActive = false;
                    finishScript(planSuccess && !queryFailed);
                }
                return;
            }
            if (PQresultStatus(result) != PGRES_COMMAND_OK) {
                queryFailed = true;
                emit notice(QString::fromUtf8(PQresultErrorMessage(result)).trimmed());
            }
            PQclear(result);
        }
        if (notifier) notifier->setEnabled(true);
    }

    // Prepare exactly one statement and enter libpq single-row mode before flushing it.
    void nextStatement()
    {
        if (!running) return;
        if (cancelled || ++statementIndex >= statements.size()) {
            finishScript(!cancelled);
            return;
        }
        pending = QueryResult();
        pending.number = statementIndex + 1;
        pending.title = statements[statementIndex].name.isEmpty() ? tr("Query %1").arg(pending.number) : statements[statementIndex].name;
        statementRows = 0;
        statementKeptRows = 0;
        exportSectionStarted = false;
        queryFailed = false;
        complete = false;
        statementElapsed.start();
        const auto sql = statements[statementIndex].sql.toUtf8();
        if (!PQsendQuery(connection, sql.constData()) || !PQsetSingleRowMode(connection)) {
            fail(QString::fromUtf8(PQerrorMessage(connection)), false);
            return;
        }
        if (notifier) notifier->setEnabled(false);
        flushQuery();
    }

    // Retry a partial nonblocking send with timers, then start reading the statement response.
    void flushQuery()
    {
        if (!running) return;
        const int result = PQflush(connection);
        if (result < 0) { connectionFailed(); return; }
        if (result == 1) { QTimer::singleShot(10, this, &PgConnection::flushQuery); return; }
        readResults();
    }

    // Emit one bounded batch and pause socket draining until acknowledgement arrives.
    void publish()
    {
        waiting = true;
        if (notifier) notifier->setEnabled(false);
        emit resultReady(pending);
        pending.rows.clear();
    }

    // Latch/report the first file error and stop further SQL through the normal cancel protocol.
    bool checkExport(bool success)
    {
        if (success) return true;
        if (!exportReported) { emit notice(fileExport->errorString()); exportReported = true; }
        if (complete) cancelled = true;
        else cancel();
        return false;
    }

    // Convert the current libpq row before any clipping; bytea keeps its canonical hex notation.
    QStringList fullRow(PGresult *result, int row)
    {
        QStringList values;
        for (int column = 0; column < PQnfields(result); ++column) {
            if (PQgetisnull(result, row, column)) { values.append("NULL"); continue; }
            const auto *value = PQgetvalue(result, row, column);
            const int length = PQgetlength(result, row, column);
            if (PQftype(result, column) == 17 && !(length >= 2 && value[0] == '\\' && value[1] == 'x')) {
                size_t size = 0;
                auto *bytes = PQunescapeBytea(reinterpret_cast<const unsigned char *>(value), &size);
                values.append("\\x" + QString::fromLatin1(QByteArray(reinterpret_cast<const char *>(bytes), int(size)).toHex()));
                PQfreemem(bytes);
            } else values.append(QString::fromUtf8(value, length));
        }
        return values;
    }

    // Drain libpq in short slices, bounding stored rows/bytes and explicitly marking clipping/errors.
    void readResults()
    {
        if (!running || waiting) return;
        if (planControl) { readPlanControl(); return; }
        if (fileExport && exportCancelled->load() && !cancelled) cancel();
        if (complete) {
            if (queryFailed) finishScript(false);
            else if (const auto *setting = PQparameterStatus(connection, "standard_conforming_strings"); setting && QByteArray(setting) != "on") {
                emit notice(tr("Script gestopt: standard_conforming_strings moet aan staan voor de SQL-editor."));
                finishScript(false);
            } else nextStatement();
            return;
        }
        if (PQconsumeInput(connection) == 0) { connectionFailed(); return; }
        QElapsedTimer slice;
        slice.start();
        while (!PQisBusy(connection)) {
            PGresult *result = PQgetResult(connection);
            if (!result) {
                complete = true;
                pending.finished = true;
                pending.elapsedMs = statementElapsed.elapsed();
                if (fileExport && !cancelled) checkExport(fileExport->endResult(pending.status +
                    tr(" | %1 ms (client, inclusief ophalen)").arg(pending.elapsedMs)));
                if (pending.truncated) pending.status += tr(" — weergavelimiet bereikt; uitvoer ingekort");
                pending.status += tr(" | %1 ms (client, inclusief ophalen)").arg(pending.elapsedMs);
                publish();
                return;
            }
            const auto status = PQresultStatus(result);
            if (status == PGRES_COPY_IN || status == PGRES_COPY_OUT || status == PGRES_COPY_BOTH) {
                PQclear(result);
                fail(tr("COPY via de editor wordt nog niet ondersteund. Verbinding gesloten om de protocolstatus te herstellen."), false);
                return;
            }
            if (pending.columns.isEmpty())
                for (int col = 0; col < PQnfields(result); ++col) pending.columns.append(QString::fromUtf8(PQfname(result, col)));
            if (fileExport && !exportSectionStarted && !cancelled) {
                exportSectionStarted = true;
                checkExport(fileExport->beginResult(pending.number, pending.title, pending.columns));
            }
            if (status == PGRES_SINGLE_TUPLE || status == PGRES_TUPLES_OK) {
                for (int row = 0; row < PQntuples(result); ++row) {
                    ++statementRows;
                    if (fileExport && !cancelled && !exportCancelled->load() && checkExport(fileExport->writeRow(fullRow(result, row)))) {
                        ++exportRows;
                        if (exportClock.elapsed() >= 250) { emit exportProgress(exportRows); exportClock.restart(); }
                    }
                    if (statementKeptRows >= 20000 || keptRows >= 50000 || keptBytes + 128LL * PQnfields(result) + 32 >= 32 * 1024 * 1024) {
                        pending.truncated = true;
                        continue;
                    }
                    QVector<ResultCell> cells;
                    keptBytes += 128LL * PQnfields(result) + 32;
                    for (int col = 0; col < PQnfields(result); ++col) {
                        ResultCell cell;
                        cell.isNull = PQgetisnull(result, row, col) != 0;
                        cell.isBinary = PQftype(result, col) == 17; // PostgreSQL bytea
                        cell.sourceBytes = PQgetlength(result, row, col);
                        if (!cell.isNull) {
                            const int available = int(qMax<qint64>(0, 32 * 1024 * 1024 - keptBytes));
                            const int length = std::min({int(cell.sourceBytes), 2 * 1024 * 1024, available / 2});
                            cell.truncated = length < cell.sourceBytes;
                            const auto *value = PQgetvalue(result, row, col);
                            if (cell.isBinary) {
                                // libpq defaults to hex bytea on modern PostgreSQL; retain raw bytes for save/hex viewing.
                                if (cell.sourceBytes >= 2 && value[0] == '\\' && value[1] == 'x') {
                                    cell.binary = QByteArray::fromHex(QByteArray(value + 2, qMax(0, length - 2) / 2 * 2));
                                    cell.sourceBytes = (cell.sourceBytes - 2) / 2;
                                } else if (!cell.truncated) {
                                    size_t size = 0;
                                    auto *bytes = PQunescapeBytea(reinterpret_cast<const unsigned char *>(value), &size);
                                    if (bytes) {
                                        cell.binary = QByteArray(reinterpret_cast<const char *>(bytes), int(size));
                                        PQfreemem(bytes);
                                    }
                                    cell.sourceBytes = cell.binary.size();
                                }
                                keptBytes += cell.binary.size();
                            } else {
                                cell.text = QString::fromUtf8(value, length);
                                keptBytes += cell.text.size() * 2;
                            }
                        }
                        pending.truncated |= cell.truncated;
                        cells.append(cell);
                    }
                    pending.rows.append(cells);
                    ++statementKeptRows;
                    ++keptRows;
                }
                if (status == PGRES_TUPLES_OK) pending.status = tr("%1 rijen").arg(statementRows);
            } else if (status == PGRES_COMMAND_OK || status == PGRES_EMPTY_QUERY) {
                pending.status = QString::fromUtf8(PQcmdStatus(result));
            } else {
                pending.error = queryFailed = true;
                pending.status = QString::fromUtf8(PQresultErrorMessage(result)).trimmed();
                if (const auto *state = PQresultErrorField(result, PG_DIAG_SQLSTATE)) pending.sqlState = QString::fromLatin1(state);
            }
            PQclear(result);
            if (pending.rows.size() >= 128 || slice.elapsed() >= 5) {
                if (!pending.rows.isEmpty()) publish();
                else QTimer::singleShot(0, this, &PgConnection::readResults);
                return;
            }
        }
        if (!pending.rows.isEmpty()) publish();
        else if (notifier) notifier->setEnabled(true);
    }

    PGconn *connection = nullptr;
    QSocketNotifier *notifier = nullptr;
    QTimer *timeout;
    QElapsedTimer elapsed;
    bool established = false;
    int lookup = -1;
    QVector<SqlStatement> statements;
    int statementIndex = -1;
    QueryResult pending;
    QElapsedTimer statementElapsed;
    QElapsedTimer cancelElapsed;
#ifdef LIBPQ_HAS_ASYNC_CANCEL
    PGcancelConn *cancelConnection = nullptr;
    PostgresPollingStatusType cancelPollStatus = PGRES_POLLING_WRITING;
#else
    QFutureWatcher<bool> *cancelTask = nullptr;
#endif
    bool running = false;
    bool waiting = false;
    bool complete = false;
    bool queryFailed = false;
    bool cancelled = false;
    bool planGuardActive = false;
    bool planControl = false;
    bool planStarting = false;
    bool planSuccess = false;
    QString planCleanup;
    std::shared_ptr<std::atomic_bool> exportCancelled;
    std::unique_ptr<FullResultExport> fileExport;
    QString exportPath;
    qint64 exportRows = 0;
    QElapsedTimer exportClock;
    bool exportSectionStarted = false;
    bool exportReported = false;
    qint64 keptBytes = 0;
    int keptRows = 0;
    qint64 statementRows = 0;
    int statementKeptRows = 0;
};

// Own a worker thread and forward commands/results; acknowledge each delivered batch to bound queued work.
PgSession::PgSession(QObject *parent) : QObject(parent), worker(new PgConnection(exportCancelled))
{
    qRegisterMetaType<QueryResult>();
    worker->moveToThread(&thread);
    connect(&thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(this, &PgSession::openRequested, worker, &PgConnection::open);
    connect(this, &PgSession::closeRequested, worker, &PgConnection::close);
    connect(this, &PgSession::executeRequested, worker, &PgConnection::execute);
    connect(this, &PgSession::exportRequested, worker, &PgConnection::executeToFile);
    connect(this, &PgSession::roleChangeRequested, worker, &PgConnection::changeRole);
    connect(this, &PgSession::cancelRequested, worker, &PgConnection::cancel);
    connect(this, &PgSession::acknowledged, worker, &PgConnection::acknowledge);
    connect(worker, &PgConnection::resultReady, this, [this](const QueryResult &result) {
        emit resultReady(result);
        emit acknowledged();
    });
    connect(worker, &PgConnection::notice, this, &PgSession::notice);
    connect(worker, &PgConnection::exportProgress, this, &PgSession::exportProgress);
    connect(worker, &PgConnection::exportFinished, this, &PgSession::exportFinished);
    connect(worker, &PgConnection::transactionChanged, this, [this](bool active) { transactionActive = active; });
    connect(worker, &PgConnection::executionFinished, this, [this](bool success, const QString &transaction) {
        busy = false;
        emit executionFinished(success, transaction);
        emit stateChanged();
    });
    connect(worker, &PgConnection::connected, this, [this](const QVariantMap &info) {
        if (connectionState != Connecting) return;
        connectionState = Connected;
        serverInformation = info;
        emit stateChanged();
        emit connected(info);
    });
    connect(worker, &PgConnection::failed, this, [this](const QString &message, bool needsPassword) {
        if (connectionState == Disconnecting) return;
        connectionState = Disconnected;
        transactionActive = false;
        serverInformation.clear();
        emit stateChanged();
        emit failed(message, needsPassword);
    });
    connect(worker, &PgConnection::disconnected, this, [this] {
        connectionState = Disconnected;
        transactionActive = false;
        serverInformation.clear();
        emit stateChanged();
        emit disconnected();
    });
    thread.start();
}

// Stop the worker event loop and wait until its PGconn and notifier resources have been released.
PgSession::~PgSession()
{
    exportCancelled->store(true);
    thread.quit();
    thread.wait();
}

// Enter Connecting before queueing a worker login, ignoring duplicate requests for a live session.
void PgSession::open(const QVariantMap &profile, const QString &password)
{
    if (connectionState != Disconnected) return;
    connectionState = Connecting;
    emit stateChanged();
    emit openRequested(profile, password);
}

// Mark the session Disconnecting before asking the worker to release the connection.
void PgSession::close()
{
    exportCancelled->store(true);
    if (connectionState == Disconnected || connectionState == Disconnecting) return;
    connectionState = Disconnecting;
    emit stateChanged();
    emit closeRequested();
}

// Mark a connected idle session busy before queueing SQL; report acceptance rather than server success.
bool PgSession::execute(const QString &sql, bool rollbackExplain)
{
    if (connectionState != Connected || busy) return false;
    busy = true;
    exportCancelled->store(false);
    emit stateChanged();
    emit executeRequested(sql, rollbackExplain);
    return true;
}

// Capture explicit export arguments without replaying an already executed result.
bool PgSession::executeToFile(const QString &sql, const QString &path, bool excel, const QVector<int> &widths, bool rollbackExplain)
{
    if (connectionState != Connected || busy || path.isEmpty()) return false;
    exportCancelled->store(false);
    busy = true;
    emit stateChanged();
    emit exportRequested(sql, path, excel, widths, rollbackExplain);
    return true;
}

// Queue a password-bearing role operation separately from ordinary query text.
bool PgSession::executeRoleChange(const QString &sql, const QString &role, const QString &password)
{
    if (role.isEmpty() || password.isEmpty() || role.contains(QChar(0)) || password.contains(QChar(0))) return false;
    if (connectionState != Connected || busy) return false;
    busy = true;
    emit stateChanged();
    emit roleChangeRequested(sql, role, password);
    return true;
}

// Forward cancellation only while a script is active; the worker handles the libpq cancel protocol.
void PgSession::cancel()
{
    if (busy) { exportCancelled->store(true); emit cancelRequested(); }
}

#include "pgsession.moc"
