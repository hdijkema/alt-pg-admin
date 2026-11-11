#ifndef PGADMMAIN_H
#define PGADMMAIN_H

#include <QMainWindow>
#include <QSettings>
#include <QHash>
#include <QSet>
#include "administration/databasejob.h"

class QTabWidget;
class QTreeWidget;
class QueryTab;
class PgSession;
class QAction;
class QLabel;
class PasswordStore;
class CatalogTree;
class SavedQueriesPanel;
class ObjectDetails;
class QDockWidget;
class DatabaseTaskDialog;
class RoleManager;
class QListWidget;

// goal: Coordinate windows, local profiles, named scripts and live database sessions.
// pre: Construct after QApplication on the GUI thread; settingsPath may isolate tests.
// post: Startup reads local state only; no automatic database connection or SQL replay.
// internals: ProfileDialog edits fields, PasswordStore handles secrets, CatalogTree
//            owns metadata queues and QueryTabs receive batches from PgSessions.
//            querySessions owns one lazy private backend per tab; control sessions stay separate.
//            ObjectDetails borrows the catalog queue and reads DDL only on request.
class PgAdmMain : public QMainWindow
{
    Q_OBJECT

public:
    PgAdmMain(QWidget *parent = nullptr, const QString &settingsPath = QString());
    ~PgAdmMain() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private:
    QueryTab *newQuery(const QString &connection = QString());
    QueryTab *currentQuery() const;
    void updateTitle(QueryTab *query);
    // Rename only the tab label; empty input restores the filename/library/default title.
    void renameQueryTab(int index);
    void openQuery();
    void saveNamedQuery();
    void openNamedQuery(const QString &name);
    bool saveQuery(QueryTab *query, bool saveAs = false);
    bool mayClose(QueryTab *query);
    void closeQuery(int index);
    // Persist editor contents and targets only; restoration creates no database sessions.
    bool saveEditorTabs();
    void restoreEditorTabs();
    // After an explicit server login, reconnect restored targets and private tab sessions.
    // No editor SQL is dispatched; unresolved profiles and failed databases stay disconnected.
    void reconnectRestoredTabs(const QString &serverId);
    // Store bounded accepted SQL submissions; opening history creates a fresh inactive tab.
    void recordHistory(const QString &sql, QueryTab *tab);
    void refreshHistory();
    void loadProfiles();
    void editProfile(bool create);
    void removeProfile();
    QVariantMap selectedProfile() const;
    void connectProfile();
    void openDatabaseQuery();
    void disconnectProfile();
    void updateConnectionState();
    void runQuery();
    void executeQuery(const QString &exportPath = {}, bool excel = false);
    void exportFullResults(bool excel);
    void cancelQuery();
    void updateQueryActions();
    // Create a private session lazily; closing/rebinding affects only this tab.
    PgSession *querySession(QueryTab *tab);
    void closeQuerySession(QueryTab *tab);
    bool confirmTransactionClose(QueryTab *tab);
    QVariantMap profileById(const QString &id) const;
    QString selectedConnectionId() const;
    static QString connectionId(const QVariantMap &profile, const QString &database);
    bool serverInUse(const QString &id) const;
    void connectDatabase(const QVariantMap &profile, const QString &database, bool openQuery);
    void promptPassword(const QString &connectionId);
    void openSession(const QString &connectionId, const QString &password);
    void reportPasswordError(const QString &error);
    void updateObjectProperties();
    void openDatabaseTask(DatabaseJob::Operation operation);
    bool releaseDatabase(const QString &profileId, const QString &database);
    void openRoleManager(const QString &profileId, const QString &suggestedRole = {});

    QSettings settings;
    QListWidget *history;
    QTabWidget *queries;
    CatalogTree *objects;
    SavedQueriesPanel *savedQueries;
    ObjectDetails *objectDetails;
    QDockWidget *detailsDock;
    QAction *propertiesAction;
    QAction *backupAction;
    QAction *restoreAction;
    QAction *createDatabaseAction;
    QAction *dropDatabaseAction;
    QAction *vacuumAction;
    QAction *analyzeAction;
    QAction *reindexAction;
    DatabaseTaskDialog *databaseTask = nullptr;
    QString maintenanceServer;
    QString maintenanceDatabase;
    QAction *rolesAction;
    RoleManager *roleManager = nullptr;
    QString roleManagerServer;
    PasswordStore *passwordStore;
    QHash<QString, PgSession *> sessions;
    QHash<QueryTab *, PgSession *> querySessions;
    QHash<QueryTab *, QString> queryTargets;
    QSet<QueryTab *> restoredTabs;
    struct PendingQuery {
        QString sql;
        QString exportPath;
        bool excel = false;
        QVector<int> widths;
        bool rollbackExplain = false;
    };
    QHash<PgSession *, PendingQuery> pendingScripts;
    QHash<QString, QVariantMap> sessionProfiles;
    QHash<QString, QString> attemptedPasswords;
    QHash<QString, QString> serverPasswords;
    QHash<QString, quint64> pendingPasswordReads;
    quint64 passwordReadSequence = 0;
    QSet<QString> openQueryAfterConnect;
    QAction *connectAction;
    QAction *disconnectAction;
    QAction *databaseQueryAction;
    QLabel *connectionLabel;
    QAction *runAction;
    QAction *stopAction;
    QAction *fullCsvAction;
    QAction *fullXlsxAction;
    QHash<PgSession *, QueryTab *> executing;
    int nextQuery = 1;
};
#endif // PGADMMAIN_H
