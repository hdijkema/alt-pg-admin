#include "pgadmmain.h"
#include "query/querytab.h"
#include "database/pgsession.h"
#include <QDateTime>
#include <QFile>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextDocument>

// Save editor snapshots at an accepted normal exit; never persist sessions or query results.
bool PgAdmMain::saveEditorTabs()
{
    QVariantList tabs;
    for (int i = 0; i < queries->count(); ++i) {
        auto *tab = qobject_cast<QueryTab *>(queries->widget(i));
        const auto target = sessionProfiles.value(tab->connectionId());
        tabs.append(QVariantMap{{"sql", tab->editor()->toPlainText()}, {"path", tab->filePath()},
            {"bom", tab->hasUtf8Bom()}, {"modified", tab->editor()->document()->isModified()},
            {"target", tab->connectionId()}, {"widths", tab->columnWidths()},
            {"server", target.value("serverId")}, {"database", target.value("database")},
            {"tabName", tab->property("tabName")},
            {"library", tab->property("libraryName")}, {"cursor", tab->editor()->textCursor().position()}});
    }
    settings.setValue("editor/tabs", tabs);
    settings.setValue("editor/activeTab", queries->currentIndex());
    settings.sync();
    return settings.status() == QSettings::NoError;
}

// Restore snapshots rather than rereading files; later disk changes mark the snapshot unsaved.
void PgAdmMain::restoreEditorTabs()
{
    for (const auto &value : settings.value("editor/tabs").toList()) {
        const auto entry = value.toMap();
        auto *tab = newQuery();
        auto target = entry.value("target").toString();
        auto serverId = entry.value("server").toString();
        auto database = entry.value("database").toString();
        // Older snapshots have only the stable target ID: server ID or server/base64url(database).
        if (serverId.isEmpty()) {
            for (const auto &profileValue : settings.value("connections/profiles").toList()) {
                const auto profile = profileValue.toMap();
                const auto id = profile.value("id").toString();
                if (id.isEmpty()) continue;
                if (target == id) {
                    serverId = id;
                    database = profile.value("database").toString();
                    break;
                }
                if (target.startsWith(id + '/')) {
                    database = QString::fromUtf8(QByteArray::fromBase64(target.mid(id.size() + 1).toLatin1(),
                        QByteArray::Base64UrlEncoding | QByteArray::AbortOnBase64DecodingErrors));
                    if (!database.isEmpty()) { serverId = id; break; }
                }
            }
        }
        if (!serverId.isEmpty() && !database.isEmpty()) {
            auto runtime = profileById(serverId);
            if (!runtime.isEmpty()) target = connectionId(runtime, database);
            runtime["serverId"] = serverId;
            runtime["database"] = database;
            runtime["id"] = target;
            runtime["name"] = runtime.value("name").toString() + " / " + database;
            sessionProfiles[target] = runtime;
        }
        tab->selectConnection(target);
        if (!serverId.isEmpty() && !database.isEmpty()) restoredTabs.insert(tab);
        tab->setProperty("tabName", entry.value("tabName"));
        tab->setFilePath(entry.value("path").toString());
        tab->setUtf8Bom(entry.value("bom").toBool());
        tab->setProperty("libraryName", entry.value("library"));
        tab->setColumnWidths(entry.value("widths").toList());
        const auto sql = entry.value("sql").toString();
        tab->editor()->setPlainText(sql);
        bool modified = entry.value("modified").toBool();
        if (!tab->filePath().isEmpty()) {
            QFile file(tab->filePath());
            if (!file.open(QIODevice::ReadOnly)) modified = true;
            else {
                const auto contents = file.readAll();
                const bool bom = contents.startsWith("\xef\xbb\xbf");
                auto diskText = QString::fromUtf8(bom ? contents.mid(3) : contents);
                diskText.replace("\r\n", "\n");
                diskText.replace('\r', '\n');
                if (diskText != sql || bom != tab->hasUtf8Bom()) modified = true;
            }
        }
        tab->editor()->document()->setModified(modified);
        auto cursor = tab->editor()->textCursor();
        cursor.setPosition(qBound(0, entry.value("cursor").toInt(), tab->editor()->document()->characterCount() - 1));
        tab->editor()->setTextCursor(cursor);
        updateTitle(tab);
    }
    if (!queries->count()) newQuery();
    queries->setCurrentIndex(qBound(0, settings.value("editor/activeTab", 0).toInt(), queries->count() - 1));
}

// Reopen database controls and independent tab backends only after their server was explicitly connected.
// Connection callbacks revisit pending tabs; SQL snapshots are never sent to PostgreSQL here.
void PgAdmMain::reconnectRestoredTabs(const QString &serverId)
{
    const auto profile = profileById(serverId);
    if (profile.isEmpty()) return;
    for (auto *tab : restoredTabs.values()) {
        const auto runtime = sessionProfiles.value(tab->connectionId());
        if (runtime.value("serverId").toString() != serverId) continue;
        auto *control = sessions.value(tab->connectionId());
        if (!control || control->state() != PgSession::Connected) {
            int active = pendingPasswordReads.size();
            for (auto *session : sessions)
                if (session->state() != PgSession::Disconnected) ++active;
            if ((!control || control->state() == PgSession::Disconnected) && active >= 8) {
                restoredTabs.remove(tab);
                tab->appendMessage(tr("Verbreek eerst een van de acht actieve verbindingen."));
                continue;
            }
            connectDatabase(profile, runtime.value("database").toString(), false);
            continue;
        }
        restoredTabs.remove(tab);
        int active = 0;
        for (auto *session : querySessions)
            if (session->state() != PgSession::Disconnected) ++active;
        if (active >= 8) {
            tab->appendMessage(tr("Sluit of verbreek eerst een van de acht actieve querytabsessies."));
            continue;
        }
        if (auto *session = querySession(tab)) session->open(runtime, serverPasswords.value(serverId));
    }
}

// Keep only accepted submissions, bounded by count and text length; credentials are never copied.
void PgAdmMain::recordHistory(const QString &sql, QueryTab *tab)
{
    if (sql.trimmed().isEmpty()) return;
    if (sql.size() > 64000) {
        statusBar()->showMessage(tr("SQL groter dan 64.000 tekens wordt niet in de historie bewaard."), 8000);
        return;
    }
    auto entries = settings.value("editor/history").toList();
    const auto profile = sessionProfiles.value(tab->connectionId());
    entries.prepend(QVariantMap{{"sql", sql}, {"target", tab->connectionId()},
        {"context", profile.value("name")}, {"database", profile.value("database")},
        {"time", QDateTime::currentDateTime().toString(Qt::ISODate)}});
    while (entries.size() > 100) entries.removeLast();
    settings.setValue("editor/history", entries);
    refreshHistory();
}

// Present recent submitted SQL independently from success/failure and from named query files.
void PgAdmMain::refreshHistory()
{
    history->clear();
    for (const auto &value : settings.value("editor/history").toList()) {
        const auto entry = value.toMap();
        const auto sql = entry.value("sql").toString();
        auto *item = new QListWidgetItem(entry.value("time").toString() + " | " +
            entry.value("context").toString() + " / " + entry.value("database").toString() + "\n" +
            sql.simplified().left(140), history);
        item->setData(Qt::UserRole, entry);
        item->setToolTip(entry.value("context").toString() + " / " + entry.value("database").toString() + "\n" + sql);
        item->setHidden(!item->toolTip().contains(history->property("filter").toString(), Qt::CaseInsensitive));
    }
}
