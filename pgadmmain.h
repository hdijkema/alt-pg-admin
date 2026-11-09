#ifndef PGADMMAIN_H
#define PGADMMAIN_H

#include <QMainWindow>
#include <QSettings>
#include <QHash>
#include <QSet>
#include "databasejob.h"

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

// goal: Coordinate windows, local profiles, named scripts and live database sessions.
// pre: Construct after QApplication on the GUI thread; settingsPath may isolate tests.
// post: Startup reads local state only; no automatic database connection or SQL replay.
// internals: ProfileDialog edits fields, PasswordStore handles secrets, CatalogTree
//            owns metadata queues and QueryTabs receive batches from PgSessions.
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
    void openQuery();
    void saveNamedQuery();
    void openNamedQuery(const QString &name);
    bool saveQuery(QueryTab *query, bool saveAs = false);
    bool mayClose(QueryTab *query);
    void closeQuery(int index);
    void loadProfiles();
    void editProfile(bool create);
    void removeProfile();
    QVariantMap selectedProfile() const;
    void connectProfile();
    void openDatabaseQuery();
    void disconnectProfile();
    void updateConnectionState();
    void runQuery();
    void cancelQuery();
    void updateQueryActions();
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
    DatabaseTaskDialog *databaseTask = nullptr;
    QString maintenanceServer;
    QString maintenanceDatabase;
    QAction *rolesAction;
    RoleManager *roleManager = nullptr;
    QString roleManagerServer;
    PasswordStore *passwordStore;
    QHash<QString, PgSession *> sessions;
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
    QHash<PgSession *, QueryTab *> executing;
    int nextQuery = 1;
};
#endif // PGADMMAIN_H
