#include "pgadmmain.h"
#include "querytab.h"
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
        tabs.append(QVariantMap{{"sql", tab->editor()->toPlainText()}, {"path", tab->filePath()},
            {"bom", tab->hasUtf8Bom()}, {"modified", tab->editor()->document()->isModified()},
            {"target", tab->connectionId()}, {"widths", tab->columnWidths()},
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
        tab->selectConnection(entry.value("target").toString());
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
