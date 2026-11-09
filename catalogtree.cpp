#include "catalogtree.h"
#include "catalogservice.h"
#include <QStyle>

// Wire lazy expansion and route catalog replies through persistent indexes to surviving nodes.
CatalogTree::CatalogTree(QWidget *parent) : QTreeWidget(parent), catalog(new CatalogService(this))
{
    setObjectName("catalogTree");
    setHeaderLabel(tr("Servers en databases"));
    setMinimumWidth(250);
    connect(this, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) {
        if (item->data(0, NodeKind).toInt() == Server) {
            const auto id = item->data(0, ProfileId).toString();
            if (!connectedProfiles.contains(id)) emit connectServerRequested(id);
        } else load(item);
    });
    connect(catalog, &CatalogService::loaded, this, [this](quint64 id, const QVariantList &rows) {
        const auto index = pending.take(id);
        if (!index.isValid()) return;
        auto *item = itemFromIndex(index);
        item->setData(0, Loading, false);
        item->setData(0, Loaded, true);
        populate(item, rows);
    });
    connect(catalog, &CatalogService::failed, this, [this](quint64 id, const QString &message) {
        const auto index = pending.take(id);
        if (!index.isValid()) return;
        auto *item = itemFromIndex(index);
        item->setData(0, Loading, false);
        item->setData(0, Loaded, false);
        qDeleteAll(item->takeChildren());
        auto *error = add(item, tr("Ophalen mislukt — vernieuw om opnieuw te proberen"), Info);
        error->setToolTip(0, message);
        item->setToolTip(0, message);
    });
}

// Find a root by stable profile ID rather than its editable display name.
QTreeWidgetItem *CatalogTree::serverItem(const QString &id) const
{
    for (int i = 0; i < topLevelItemCount(); ++i)
        if (topLevelItem(i)->data(0, ProfileId).toString() == id) return topLevelItem(i);
    return nullptr;
}

// Create a child that inherits connection/object context before specialising its node kind.
QTreeWidgetItem *CatalogTree::add(QTreeWidgetItem *parent, const QString &name, Kind kind, bool expandable)
{
    auto *item = new QTreeWidgetItem(parent, {name});
    for (int role : {ProfileId, DatabaseName, SchemaName, RelationName}) item->setData(0, role, parent->data(0, role));
    item->setData(0, NodeKind, kind);
    item->setIcon(0, style()->standardIcon(kind == Database ? QStyle::SP_DriveHDIcon : QStyle::SP_DirIcon));
    if (expandable) item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    return item;
}

// Replace root nodes from local settings, invalidating old pending tree indexes.
void CatalogTree::setProfiles(const QVariantList &profiles)
{
    pending.clear();
    clear();
    for (const auto &value : profiles) {
        const auto profile = value.toMap();
        auto *root = new QTreeWidgetItem(this, {profile.value("name").toString()});
        root->setData(0, ProfileId, profile.value("id"));
        root->setData(0, NodeKind, Server);
        add(root, tr("Databases — verbind om te openen"), DatabaseList, true);
        if (connectedProfiles.contains(profile.value("id").toString()))
            serverConnected(profile, passwords.value(profile.value("id").toString()));
    }
}

// Cache credentials in memory and expand the database branch after a successful server login.
void CatalogTree::serverConnected(const QVariantMap &profile, const QString &password)
{
    const auto id = profile.value("id").toString();
    connectedProfiles[id] = profile;
    passwords[id] = password;
    auto *root = serverItem(id);
    if (!root) return;
    if (!root->childCount()) add(root, tr("Databases"), DatabaseList, true);
    auto *databases = root->child(0);
    databases->setText(0, tr("Databases"));
    root->setExpanded(true);
    databases->setExpanded(true);
    load(databases);
}

// Close metadata sessions and replace descendants with a reconnectable placeholder.
void CatalogTree::serverDisconnected(const QString &id)
{
    connectedProfiles.remove(id);
    passwords.remove(id);
    catalog->closeServer(id);
    auto *root = serverItem(id);
    if (!root) return;
    qDeleteAll(root->takeChildren());
    add(root, tr("Databases — verbind om te openen"), DatabaseList, true);
}

QString CatalogTree::selectedDatabase() const
{
    return currentItem() ? currentItem()->data(0, DatabaseName).toString() : QString();
}

bool CatalogTree::selectedIsDatabase() const
{
    return currentItem() && currentItem()->data(0, NodeKind).toInt() == Database;
}

// Walk to a loadable branch and request fresh metadata without duplicating an in-flight load.
void CatalogTree::refreshSelected()
{
    auto *item = currentItem();
    if (!item) return;
    if (item->data(0, NodeKind).toInt() == Server && item->childCount()) item = item->child(0);
    while (item && (item->data(0, NodeKind).toInt() == Info || item->data(0, NodeKind).toInt() == Folder || item->data(0, NodeKind).toInt() == Column)) item = item->parent();
    if (!item || item->data(0, Loading).toBool()) return;
    item->setData(0, Loaded, false);
    load(item);
}

// Translate node context into a catalog request and retain its persistent index for the reply.
void CatalogTree::load(QTreeWidgetItem *item)
{
    if (item->isDisabled()) return;
    const auto kind = Kind(item->data(0, NodeKind).toInt());
    if (kind != DatabaseList && kind != Database && kind != Schema && kind != Relation) return;
    if (item->data(0, Loaded).toBool() || item->data(0, Loading).toBool()) return;
    const auto id = item->data(0, ProfileId).toString();
    if (!connectedProfiles.contains(id)) { emit connectServerRequested(id); return; }
    auto profile = connectedProfiles.value(id);
    if (!item->data(0, DatabaseName).toString().isEmpty()) profile["database"] = item->data(0, DatabaseName);
    qDeleteAll(item->takeChildren());
    add(item, tr("Ophalen..."), Info);
    item->setData(0, Loading, true);
    const auto request = catalog->request(profile, passwords.value(id),
        kind == DatabaseList ? CatalogService::Databases : kind == Database ? CatalogService::Schemas :
        kind == Schema ? CatalogService::Relations : CatalogService::Columns,
        item->data(0, SchemaName).toString(), item->data(0, RelationName).toString());
    pending.insert(request, indexFromItem(item));
}

// Replace one branch with database/schema/relation/column nodes and explicit access/empty states.
void CatalogTree::populate(QTreeWidgetItem *item, const QVariantList &rows)
{
    qDeleteAll(item->takeChildren());
    const auto kind = Kind(item->data(0, NodeKind).toInt());
    QHash<QString, QTreeWidgetItem *> folders;
    for (const auto &value : rows) {
        const auto row = value.toMap();
        const auto name = row.value("name").toString();
        if (kind == DatabaseList) {
            auto *database = add(item, name, Database, true);
            database->setData(0, DatabaseName, name);
            database->setData(0, Accessible, row.value("accessible").toString() == "t");
            if (row.value("accessible").toString() != "t") {
                database->setDisabled(true);
                database->setToolTip(0, tr("Geen CONNECT-recht of verbindingen uitgeschakeld voor deze database."));
            }
            if (busyDatabases.value(item->data(0, ProfileId).toString()).contains(name)) {
                database->setDisabled(true);
                database->setToolTip(0, tr("Onderhoudstaak bezig voor deze database."));
            }
        } else if (kind == Database) {
            auto *schema = add(item, name, Schema, true);
            schema->setData(0, SchemaName, name);
        } else if (kind == Schema) {
            const auto relationKind = row.value("kind").toString();
            const auto label = relationKind == "v" ? tr("Views") : relationKind == "m" ? tr("Materialized views") :
                relationKind == "S" ? tr("Sequences") : tr("Tabellen");
            if (!folders.contains(label)) folders[label] = add(item, label, Folder);
            auto *relation = add(folders[label], name, Relation, true);
            relation->setData(0, RelationName, name);
            relation->setToolTip(0, item->data(0, SchemaName).toString() + "." + name);
        } else if (kind == Relation) {
            auto *column = add(item, name + " : " + row.value("type").toString() +
                (row.value("not_null").toString() == "t" ? " NOT NULL" : ""), Column);
            column->setData(0, ColumnName, name);
            column->setIcon(0, style()->standardIcon(QStyle::SP_FileIcon));
        }
    }
    if (rows.isEmpty()) add(item, tr("Geen objecten"), Info);
}

// Use the already loaded list for a server-context task's explicit database selection.
QStringList CatalogTree::databaseNames(const QString &profileId) const
{
    QStringList names;
    auto *root = serverItem(profileId);
    if (!root || !root->childCount()) return names;
    auto *list = root->child(0);
    for (int index = 0; index < list->childCount(); ++index)
        if (list->child(index)->data(0, NodeKind).toInt() == Database)
            names.append(list->child(index)->data(0, DatabaseName).toString());
    return names;
}

// Invalidate an older list reply so completion cannot restore a pre-task database list.
void CatalogTree::refreshDatabases(const QString &profileId)
{
    auto *root = serverItem(profileId);
    if (!root || !root->childCount() || !connectedProfiles.contains(profileId)) return;
    auto *list = root->child(0);
    const auto index = indexFromItem(list);
    for (auto request = pending.begin(); request != pending.end();) {
        if (request.value() == index) request = pending.erase(request);
        else ++request;
    }
    list->setData(0, Loaded, false);
    list->setData(0, Loading, false);
    load(list);
}

// Mark the target unavailable locally, without changing PostgreSQL permissions or server state.
void CatalogTree::setDatabaseBusy(const QString &profileId, const QString &database, bool busy)
{
    if (busy) busyDatabases[profileId].insert(database);
    else busyDatabases[profileId].remove(database);
    auto *root = serverItem(profileId);
    if (!root || !root->childCount()) return;
    for (int index = 0; index < root->child(0)->childCount(); ++index) {
        auto *item = root->child(0)->child(index);
        if (item->data(0, DatabaseName).toString() != database) continue;
        item->setDisabled(busy || !item->data(0, Accessible).toBool());
        item->setToolTip(0, busy ? tr("Onderhoudstaak bezig voor deze database.") : QString());
    }
}
