#ifndef CATALOGTREE_H
#define CATALOGTREE_H
#include <QTreeWidget>
#include <QPersistentModelIndex>
#include <QHash>
#include <QSet>

class CatalogService;
// goal: Browse server/database/schema/relation/column names lazily.
// pre: GUI-thread use; profiles contain stable IDs and no stored passwords.
// post: Nodes retain their database context. Deleted nodes ignore stale replies.
// internals: load() queues a CatalogService request; pending stores its persistent
//            model index, and populate() creates only the returned branch.
class CatalogTree : public QTreeWidget
{
    Q_OBJECT
public:
    enum Kind { Server, DatabaseList, Database, Schema, Relation, Column, Folder, Info };
    enum Role { ProfileId = Qt::UserRole, NodeKind, DatabaseName, SchemaName, RelationName, Loaded, Loading, ColumnName, Accessible };
    // Borrow the existing metadata queue; detail views must not create a second pool.
    CatalogService *metadata() const { return catalog; }
    explicit CatalogTree(QWidget *parent = nullptr);
    // Rebuild root nodes from local profiles; this does not connect to a server.
    void setProfiles(const QVariantList &profiles);
    // Register successful connection credentials in memory and load its database list.
    void serverConnected(const QVariantMap &profile, const QString &password);
    // Drop server metadata sessions, cached credentials and descendant nodes.
    void serverDisconnected(const QString &id);
    QString selectedDatabase() const;
    bool selectedIsDatabase() const;
    // Reload the selected branch, or its loadable parent for folders and columns.
    void refreshSelected();
    QTreeWidgetItem *serverItem(const QString &id) const;
    // Return the loaded database names under one server, without opening a connection.
    QStringList databaseNames(const QString &profileId) const;
    // Refresh this server's list after a task, without changing selection on another server.
    void refreshDatabases(const QString &profileId);
    // Keep a restore/drop target disabled across tree refreshes until the task completes.
    void setDatabaseBusy(const QString &profileId, const QString &database, bool busy);
signals:
    void connectServerRequested(const QString &id);
private:
    void load(QTreeWidgetItem *item);
    void populate(QTreeWidgetItem *item, const QVariantList &rows);
    QTreeWidgetItem *add(QTreeWidgetItem *parent, const QString &name, Kind kind, bool expandable = false);
    CatalogService *catalog;
    QHash<QString, QVariantMap> connectedProfiles;
    QHash<QString, QString> passwords;
    QHash<quint64, QPersistentModelIndex> pending;
    QHash<QString, QSet<QString>> busyDatabases;
};
#endif
