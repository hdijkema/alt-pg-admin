#include "pgadmmain.h"
#include "querytab.h"
#include "pgsession.h"
#include "passwordstore.h"
#include "catalogtree.h"
#include "savedqueriespanel.h"
#include "profiledialog.h"
#include "objectdetails.h"
#include "databasetaskdialog.h"
#include "catalogservice.h"
#include "rolemanager.h"
#include <QDir>
#include <QCryptographicHash>
#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDockWidget>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QLabel>
#include <QInputDialog>
#include <QIcon>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyle>
#include <QPushButton>
#include <QTabWidget>
#include <QTextDocument>
#include <QToolBar>
#include <QTreeWidget>
#include <QUuid>

namespace {
// Use a stable per-file layout key, treating case variants as the same Windows path.
QString queryWidthsKey(const QString &path)
{
    auto normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
    normalized = normalized.toCaseFolded();
#endif
    return "query/layouts/" + QString::fromLatin1(QCryptographicHash::hash(normalized.toUtf8(), QCryptographicHash::Sha256).toHex());
}

// Honour an explicit test/settings path; otherwise use Qt's application config directory.
QString settingsFile()
{
    const auto args = QCoreApplication::arguments();
    const int index = args.indexOf("--settings-file");
    if (index >= 0 && index + 1 < args.size()) return args.at(index + 1);
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/settings.ini";
}
}

// Build the local UI and restore settings; database work starts only after a user action.
PgAdmMain::PgAdmMain(QWidget *parent, const QString &settingsPath)
    : QMainWindow(parent), settings(settingsPath.isEmpty() ? settingsFile() : settingsPath, QSettings::IniFormat),
      queries(new QTabWidget(this)), objects(new CatalogTree(this)), passwordStore(new PasswordStore(this))
{
    setWindowTitle(tr("PgAdm Qt"));
    resize(1200, 800);
    setMinimumSize(800, 550);
    queries->setTabsClosable(true);
    queries->setObjectName("queryTabs");
    queries->setMovable(true);
    setCentralWidget(queries);
    connect(queries, &QTabWidget::tabCloseRequested, this, &PgAdmMain::closeQuery);
    connectAction = new QAction(tr("Verbinden"), this);
    connectAction->setObjectName("connectProfileAction");
    disconnectAction = new QAction(tr("Verbinding verbreken / poging annuleren"), this);
    disconnectAction->setObjectName("disconnectProfileAction");
    databaseQueryAction = new QAction(tr("Querytab openen"), this);
    databaseQueryAction->setObjectName("openDatabaseQueryAction");
    connect(databaseQueryAction, &QAction::triggered, this, &PgAdmMain::openDatabaseQuery);
    connect(connectAction, &QAction::triggered, this, &PgAdmMain::connectProfile);
    connect(disconnectAction, &QAction::triggered, this, &PgAdmMain::disconnectProfile);
    backupAction = new QAction(tr("Databasebackup..."), this);
    backupAction->setObjectName("backupDatabaseAction");
    restoreAction = new QAction(tr("Database herstellen..."), this);
    restoreAction->setObjectName("restoreDatabaseAction");
    createDatabaseAction = new QAction(tr("Database aanmaken..."), this);
    createDatabaseAction->setObjectName("createDatabaseAction");
    dropDatabaseAction = new QAction(tr("Database verwijderen..."), this);
    dropDatabaseAction->setObjectName("dropDatabaseAction");
    connect(backupAction, &QAction::triggered, this, [this] { openDatabaseTask(DatabaseJob::Backup); });
    connect(restoreAction, &QAction::triggered, this, [this] { openDatabaseTask(DatabaseJob::Restore); });
    connect(createDatabaseAction, &QAction::triggered, this, [this] { openDatabaseTask(DatabaseJob::Create); });
    connect(dropDatabaseAction, &QAction::triggered, this, [this] { openDatabaseTask(DatabaseJob::Drop); });
    rolesAction = new QAction(tr("Rollen en databaserechten..."), this);
    rolesAction->setObjectName("manageRolesAction");
    connect(rolesAction, &QAction::triggered, this, [this] { openRoleManager(selectedProfile().value("id").toString()); });
    auto *browser = new QDockWidget(tr("Objectbrowser"), this);
    browser->setObjectName("objectBrowser");
    browser->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    objects->setHeaderLabel(tr("Servers en databases"));
    objects->setMinimumWidth(230);
    browser->setWidget(objects);
    addDockWidget(Qt::LeftDockWidgetArea, browser);
    detailsDock = new QDockWidget(tr("Objecteigenschappen"), this);
    detailsDock->setObjectName("objectDetailsDock");
    objectDetails = new ObjectDetails(objects->metadata(), detailsDock);
    detailsDock->setWidget(objectDetails);
    addDockWidget(Qt::BottomDockWidgetArea, detailsDock);
    detailsDock->hide();
    propertiesAction = new QAction(tr("Eigenschappen / definitie-SQL"), this);
    propertiesAction->setObjectName("objectPropertiesAction");
    connect(propertiesAction, &QAction::triggered, this, [this] {
        const bool visible = detailsDock->isVisible();
        detailsDock->show();
        detailsDock->raise();
        if (visible) updateObjectProperties();
    });
    connect(detailsDock, &QDockWidget::visibilityChanged, this, [this](bool visible) {
        if (visible) updateObjectProperties();
        else objectDetails->clear();
    });
    auto *savedDock = new QDockWidget(tr("Bewaarde queries"), this);
    savedDock->setObjectName("savedQueriesDock");
    savedQueries = new SavedQueriesPanel(QFileInfo(settings.fileName()).absoluteDir().filePath("queries"), savedDock);
    savedDock->setWidget(savedQueries);
    addDockWidget(Qt::RightDockWidgetArea, savedDock);
    connect(savedQueries, &SavedQueriesPanel::saveRequested, this, &PgAdmMain::saveNamedQuery);
    connect(savedQueries, &SavedQueriesPanel::openRequested, this, &PgAdmMain::openNamedQuery);
    connect(savedQueries, &SavedQueriesPanel::queryRenamed, this, [this](const QString &from, const QString &to) {
        const auto oldKey = queryWidthsKey(savedQueries->library().filePath(from));
        const auto newKey = queryWidthsKey(savedQueries->library().filePath(to));
        if (settings.contains(oldKey)) settings.setValue(newKey, settings.value(oldKey));
        settings.remove(oldKey);
        for (int i = 0; i < queries->count(); ++i) {
            auto *tab = qobject_cast<QueryTab *>(queries->widget(i));
            if (tab->property("libraryName").toString() != from) continue;
            tab->setProperty("libraryName", to);
            tab->setFilePath(savedQueries->library().filePath(to));
            updateTitle(tab);
        }
    });
    connect(savedQueries, &SavedQueriesPanel::queryRemoved, this, [this](const QString &name) {
        settings.remove(queryWidthsKey(savedQueries->library().filePath(name)));
        for (int i = 0; i < queries->count(); ++i) {
            auto *tab = qobject_cast<QueryTab *>(queries->widget(i));
            if (tab->property("libraryName").toString() != name) continue;
            tab->setProperty("libraryName", QString());
            tab->setFilePath(QString());
            tab->editor()->document()->setModified(true);
            updateTitle(tab);
        }
    });
    objects->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(objects, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        objects->setCurrentItem(objects->itemAt(point));
        QMenu menu(this);
        menu.addAction(tr("Nieuw profiel..."), this, [this] { editProfile(true); });
        if (objects->currentItem()) {
            const auto kind = objects->currentItem()->data(0, CatalogTree::NodeKind).toInt();
            if (kind == CatalogTree::Server || kind == CatalogTree::DatabaseList || kind == CatalogTree::Database) {
                menu.addAction(createDatabaseAction);
                menu.addAction(backupAction);
                menu.addAction(restoreAction);
                menu.addAction(dropDatabaseAction);
                menu.addAction(rolesAction);
                menu.addSeparator();
            }
            if (propertiesAction->isEnabled()) menu.addAction(propertiesAction);
            if (!objects->selectedDatabase().isEmpty()) {
                menu.addAction(databaseQueryAction);
                menu.addSeparator();
            }
            menu.addAction(connectAction);
            menu.addAction(disconnectAction);
            menu.addAction(tr("Vernieuwen"), this, [this] {
                objects->refreshSelected();
                updateObjectProperties();
            });
            menu.addSeparator();
            menu.addAction(tr("Profiel wijzigen..."), this, [this] { editProfile(false); });
            menu.addAction(tr("Profiel verwijderen..."), this, &PgAdmMain::removeProfile);
        }
        menu.exec(objects->viewport()->mapToGlobal(point));
    });
    connect(objects, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        const auto kind = item->data(0, CatalogTree::NodeKind).toInt();
        if (kind == CatalogTree::Database) openDatabaseQuery();
        else if (kind == CatalogTree::Server) connectProfile();
    });
    connect(objects, &CatalogTree::connectServerRequested, this, [this](const QString &id) {
        const auto profile = profileById(id);
        if (!profile.isEmpty()) connectDatabase(profile, profile.value("database").toString(), false);
    });
    connect(objects, &QTreeWidget::itemSelectionChanged, this, &PgAdmMain::updateConnectionState);
    connect(objects, &QTreeWidget::itemSelectionChanged, this, &PgAdmMain::updateObjectProperties);
    auto *file = menuBar()->addMenu(tr("&Bestand"));
    auto *newAction = file->addAction(tr("Nieuwe query"), QKeySequence::New, this, [this] { newQuery(); });
    newAction->setObjectName("newQueryAction");
    auto *openAction = file->addAction(tr("SQL-bestand openen..."), QKeySequence::Open, this, &PgAdmMain::openQuery);
    auto *saveAction = file->addAction(tr("Opslaan"), QKeySequence::Save, this, [this] { saveQuery(currentQuery()); });
    saveAction->setObjectName("saveQueryAction");
    auto *saveNamed = file->addAction(tr("Opslaan in bewaarde queries..."), QKeySequence("Ctrl+Alt+S"), this, &PgAdmMain::saveNamedQuery);
    saveNamed->setObjectName("saveNamedQueryAction");
    file->addAction(tr("Opslaan als..."), QKeySequence::SaveAs, this, [this] { saveQuery(currentQuery(), true); });
    file->addAction(tr("Tab sluiten"), QKeySequence::Close, this, [this] { closeQuery(queries->currentIndex()); });
    file->addSeparator();
    file->addAction(tr("Afsluiten"), QKeySequence::Quit, this, &QWidget::close);
    auto *connection = menuBar()->addMenu(tr("&Verbinding"));
    connection->addAction(connectAction);
    connection->addAction(disconnectAction);
    connection->addAction(tr("Objectboom vernieuwen"), this, [this] {
        objects->refreshSelected();
        updateObjectProperties();
    });
    connection->addAction(propertiesAction);
    connection->addSeparator();
    connection->addAction(createDatabaseAction);
    connection->addAction(backupAction);
    connection->addAction(restoreAction);
    connection->addAction(dropDatabaseAction);
    connection->addAction(rolesAction);
    connection->addSeparator();
    auto *profileAction = connection->addAction(tr("Nieuw profiel..."), this, [this] { editProfile(true); });
    connection->addAction(tr("Profiel wijzigen..."), this, [this] { editProfile(false); });
    connection->addAction(tr("Profiel verwijderen..."), this, &PgAdmMain::removeProfile);
    auto *query = menuBar()->addMenu(tr("&Query"));
    query->addAction(databaseQueryAction);
    runAction = query->addAction(tr("Selectie / script uitvoeren"), this, &PgAdmMain::runQuery);
    runAction->setShortcut(Qt::Key_F5);
    runAction->setEnabled(false);
    stopAction = query->addAction(tr("Query annuleren"), this, &PgAdmMain::cancelQuery);
    stopAction->setEnabled(false);
    connect(queries, &QTabWidget::currentChanged, this, [this] { updateQueryActions(); });
    query->addAction(tr("Uitvoeringsplan tonen"), this, [this] {
        if (currentQuery()) currentQuery()->showPlan();
    });
    auto *view = menuBar()->addMenu(tr("&Beeld"));
    view->addAction(browser->toggleViewAction());
    view->addAction(savedDock->toggleViewAction());
    view->addAction(detailsDock->toggleViewAction());
    auto *help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("Over PgAdm Qt"), this, [this] {
        QMessageBox::about(this, tr("PgAdm Qt"), tr("PgAdm Qt 0.1\nNative PostgreSQL-beheeromgeving.\n\n"
            "Verbindingen via libpq en lokale SQL-bestanden.\n"
            "Query-uitvoering met doorlopende resultaten en celdetails.\n"
            "Objectbeheer, rechten en onderhoud volgen."));
    });
    auto *toolbar = addToolBar(tr("Werkbalk"));
    toolbar->setObjectName("mainToolbar");
    toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
    toolbar->setIconSize(QSize(24, 24));
    toolbar->setStyleSheet("QToolBar { spacing: 4px; padding: 4px; } QToolButton { padding: 5px; }");
    newAction->setIcon(QIcon(":/icons/file-plus-2.svg"));
    openAction->setIcon(QIcon(":/icons/folder-open.svg"));
    saveAction->setIcon(QIcon(":/icons/save.svg"));
    profileAction->setIcon(QIcon(":/icons/server-cog.svg"));
    connectAction->setIcon(QIcon(":/icons/plug-zap.svg"));
    disconnectAction->setIcon(QIcon(":/icons/unplug.svg"));
    runAction->setIcon(QIcon(":/icons/play.svg"));
    stopAction->setIcon(QIcon(":/icons/square.svg"));
    toolbar->addAction(newAction);
    toolbar->addAction(openAction);
    toolbar->addAction(saveAction);
    toolbar->addSeparator();
    toolbar->addAction(profileAction);
    toolbar->addAction(connectAction);
    toolbar->addAction(disconnectAction);
    toolbar->addSeparator();
    toolbar->addAction(runAction);
    toolbar->addAction(stopAction);
    connectionLabel = new QLabel(tr("Niet verbonden"), this);
    statusBar()->addPermanentWidget(connectionLabel);
    statusBar()->showMessage(tr("Gereed — dubbelklik op een profiel om te verbinden."));
    loadProfiles();
    newQuery();
    restoreGeometry(settings.value("window/geometry").toByteArray());
    restoreState(settings.value("window/state").toByteArray());
    updateConnectionState();
}

PgAdmMain::~PgAdmMain()
{
    qDeleteAll(sessions);
}

// Create an editor using the requested or current database context and saved display preferences.
QueryTab *PgAdmMain::newQuery(const QString &connection)
{
    QString target = connection;
    if (target.isEmpty() && currentQuery()) target = currentQuery()->connectionId();
    auto *query = new QueryTab(this);
    query->setAllResults(settings.value("query/allResults", true).toBool());
    query->setColumnWidths(settings.value("query/columnWidths").toList());
    connect(query, &QueryTab::columnWidthsChanged, this, [this, query](const QVariantList &widths) {
        settings.setValue(query->filePath().isEmpty() ? QString("query/columnWidths") : queryWidthsKey(query->filePath()), widths);
    });
    connect(query, &QueryTab::allResultsChanged, this, [this](bool enabled) { settings.setValue("query/allResults", enabled); });
    connect(query, &QueryTab::connectionChanged, this, &PgAdmMain::updateQueryActions);
    query->setProperty("untitledName", tr("Query %1").arg(nextQuery++));
    queries->addTab(query, QString());
    queries->setCurrentWidget(query);
    connect(query->editor()->document(), &QTextDocument::modificationChanged,
            this, [this, query] { updateTitle(query); });
    updateTitle(query);
    query->editor()->setFocus();
    updateConnectionState();
    if (!target.isEmpty()) query->selectConnection(target);
    return query;
}

QueryTab *PgAdmMain::currentQuery() const
{
    return qobject_cast<QueryTab *>(queries->currentWidget());
}

// Show the library name or filename and dirty marker without altering editor contents.
void PgAdmMain::updateTitle(QueryTab *query)
{
    QString title = query->filePath().isEmpty() ? query->property("untitledName").toString()
                                               : QFileInfo(query->filePath()).fileName();
    if (!query->property("libraryName").toString().isEmpty()) title = query->property("libraryName").toString();
    if (query->editor()->document()->isModified()) title += " *";
    const int index = queries->indexOf(query);
    queries->setTabText(index, title);
    queries->setTabToolTip(index, query->filePath());
}

// Read a UTF-8 SQL file into a fresh tab, preserving a BOM and its per-file layout.
void PgAdmMain::openQuery()
{
    const auto path = QFileDialog::getOpenFileName(this, tr("SQL-bestand openen"),
        settings.value("files/directory").toString(), tr("SQL-bestanden (*.sql);;Alle bestanden (*)"));
    if (path.isEmpty()) return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        QMessageBox::warning(this, tr("Openen mislukt"), file.errorString());
        return;
    }
    const auto contents = file.readAll();
    auto *query = newQuery();
    query->setFilePath(path);
    query->setColumnWidths(settings.value(queryWidthsKey(path), settings.value("query/columnWidths")).toList());
    query->setUtf8Bom(contents.startsWith("\xEF\xBB\xBF"));
    query->editor()->setPlainText(QString::fromUtf8(query->hasUtf8Bom() ? contents.mid(3) : contents));
    query->editor()->document()->setModified(false);
    updateTitle(query);
    settings.setValue("files/directory", QFileInfo(path).absolutePath());
}

// Commit the complete editor text atomically, then update its file/library binding and dirty state.
bool PgAdmMain::saveQuery(QueryTab *query, bool saveAs)
{
    if (!query) return false;
    QString path = query->filePath();
    if (saveAs || path.isEmpty()) {
        path = QFileDialog::getSaveFileName(this, tr("SQL-bestand opslaan"),
            path.isEmpty() ? settings.value("files/directory").toString() : path,
            tr("SQL-bestanden (*.sql);;Alle bestanden (*)"));
        if (path.isEmpty()) return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Opslaan mislukt"), file.errorString());
        return false;
    }
    QByteArray contents = query->editor()->toPlainText().toUtf8();
    if (query->hasUtf8Bom()) contents.prepend("\xEF\xBB\xBF");
    if (file.write(contents) != contents.size() || !file.commit()) {
        QMessageBox::warning(this, tr("Opslaan mislukt"), file.errorString());
        return false;
    }
    query->setFilePath(path);
    settings.setValue(queryWidthsKey(path), query->columnWidths());
    query->editor()->document()->setModified(false);
    const auto name = query->property("libraryName").toString();
    if (!name.isEmpty() && path != savedQueries->library().filePath(name)) query->setProperty("libraryName", QString());
    savedQueries->refresh();
    updateTitle(query);
    settings.setValue("files/directory", QFileInfo(path).absolutePath());
    statusBar()->showMessage(tr("Opgeslagen: %1").arg(path), 5000);
    return true;
}

// Name the current script, confirm replacement, and bind the editor to the committed library file.
void PgAdmMain::saveNamedQuery()
{
    auto *tab = currentQuery();
    if (!tab) return;
    const auto current = tab->property("libraryName").toString();
    bool ok = false;
    auto name = QInputDialog::getText(this, tr("Query bewaren"), tr("Naam:"), QLineEdit::Normal,
        current.isEmpty() ? QFileInfo(tab->filePath()).completeBaseName() : current, &ok).trimmed();
    if (!ok) return;
    auto &library = savedQueries->library();
    const auto existing = library.existingName(name);
    if (!existing.isEmpty()) {
        if (existing != current && QMessageBox::question(this, tr("Query overschrijven"),
            tr("De bewaarde query '%1' overschrijven?").arg(existing)) != QMessageBox::Yes) return;
        name = existing;
    }
    QString error;
    if (!library.save(name, tab->editor()->toPlainText(), true, &error)) {
        QMessageBox::warning(this, tr("Opslaan mislukt"), error); return;
    }
    if (existing.isEmpty() && !library.setCategory(name, savedQueries->selectedCategory(), &error))
        QMessageBox::warning(this, tr("Query opgeslagen, categorie niet bewaard"), error);
    tab->setProperty("libraryName", name);
    tab->setFilePath(library.filePath(name));
    settings.setValue(queryWidthsKey(tab->filePath()), tab->columnWidths());
    tab->setUtf8Bom(false);
    tab->editor()->document()->setModified(false);
    updateTitle(tab);
    savedQueries->refresh(name);
    statusBar()->showMessage(tr("Query bewaard: %1").arg(name), 5000);
}

// Focus an existing named editor; otherwise read its file without discarding unsaved work.
void PgAdmMain::openNamedQuery(const QString &name)
{
    for (int i = 0; i < queries->count(); ++i) {
        auto *tab = qobject_cast<QueryTab *>(queries->widget(i));
        if (tab->property("libraryName").toString() != name) continue;
        queries->setCurrentWidget(tab);
        tab->editor()->setFocus();
        return;
    }
    QString error;
    const auto sql = savedQueries->library().load(name, &error);
    if (!error.isEmpty()) { QMessageBox::warning(this, tr("Openen mislukt"), error); return; }
    auto *tab = newQuery();
    tab->setProperty("libraryName", name);
    tab->setFilePath(savedQueries->library().filePath(name));
    tab->setColumnWidths(settings.value(queryWidthsKey(tab->filePath()), settings.value("query/columnWidths")).toList());
    tab->editor()->setPlainText(sql);
    tab->editor()->document()->setModified(false);
    updateTitle(tab);
}

// Prevent closing a running query and resolve unsaved changes before a tab or window closes.
bool PgAdmMain::mayClose(QueryTab *query)
{
    if (query->isRunning()) {
        QMessageBox::information(this, tr("Query wordt uitgevoerd"), tr("Annuleer eerst de query of wacht tot de uitvoering klaar is."));
        return false;
    }
    if (!query->editor()->document()->isModified()) return true;
    const auto answer = QMessageBox::question(this, tr("Niet-opgeslagen query"),
        tr("Wijzigingen in %1 opslaan?").arg(queries->tabText(queries->indexOf(query))),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Save);
    return answer == QMessageBox::Discard || (answer == QMessageBox::Save && saveQuery(query));
}

// Close only after mayClose succeeds; retain an empty editor when the last tab disappears.
void PgAdmMain::closeQuery(int index)
{
    auto *query = qobject_cast<QueryTab *>(queries->widget(index));
    if (!query || !mayClose(query)) return;
    queries->removeTab(index);
    query->deleteLater();
    if (queries->count() == 0) newQuery();
}

// Check every editor before saving window state; cancellation leaves the window open.
void PgAdmMain::closeEvent(QCloseEvent *event)
{
    if (roleManager && !roleManager->mayClose()) { event->ignore(); return; }
    if (databaseTask && databaseTask->isRunning()) {
        databaseTask->raise();
        QMessageBox::information(this, tr("Onderhoudstaak bezig"), tr("Wacht tot de taak klaar is of annuleer deze eerst in het taakvenster."));
        event->ignore();
        return;
    }
    for (int i = 0; i < queries->count(); ++i) {
        if (!mayClose(qobject_cast<QueryTab *>(queries->widget(i)))) {
            event->ignore();
            return;
        }
    }
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());
    event->accept();
}

// Rebuild local server roots and restore the selected profile without starting connections.
void PgAdmMain::loadProfiles()
{
    const auto selected = objects->currentItem() ? objects->currentItem()->data(0, Qt::UserRole).toString() : QString();
    const auto profiles = settings.value("connections/profiles").toList();
    objects->setProfiles(profiles);
    if (auto *item = objects->serverItem(selected)) objects->setCurrentItem(item);
    objects->sortItems(0, Qt::AscendingOrder);
    updateConnectionState();
}

// Collect fields through ProfileDialog, then coordinate settings, vault changes and cached secrets.
void PgAdmMain::editProfile(bool create)
{
    auto profiles = settings.value("connections/profiles").toList();
    const auto *item = objects->currentItem();
    if (!create && !item) {
        statusBar()->showMessage(tr("Selecteer eerst een verbindingsprofiel."), 5000);
        return;
    }
    int index = -1;
    for (int i = 0; !create && i < profiles.size(); ++i)
        if (profiles[i].toMap().value("id") == item->data(0, Qt::UserRole)) index = i;
    if (!create && index < 0) return;
    if (!create && serverInUse(item->data(0, Qt::UserRole).toString())) {
        QMessageBox::information(this, tr("Profiel in gebruik"), tr("Verbreek eerst de verbinding voordat je dit profiel wijzigt."));
        return;
    }
    QVariantMap profile = index < 0 ? QVariantMap() : profiles[index].toMap();
    const auto oldProfile = profile;
    QString enteredPassword;
    if (!ProfileDialog::edit(this, create, profile, enteredPassword)) return;
// Hash the server login identity while deliberately sharing credentials across its databases.
    if (!oldProfile.isEmpty() && PasswordStore::key(oldProfile) != PasswordStore::key(profile))
// Hash the server login identity while deliberately sharing credentials across its databases.
        passwordStore->remove(PasswordStore::key(oldProfile), [this](const QString &error) { reportPasswordError(error); });
    if (profile.value("rememberPassword").toBool() && !enteredPassword.isEmpty())
// Hash the server login identity while deliberately sharing credentials across its databases.
        passwordStore->write(PasswordStore::key(profile), enteredPassword, [this](const QString &error) { reportPasswordError(error); });
    else if (!profile.value("rememberPassword").toBool())
// Hash the server login identity while deliberately sharing credentials across its databases.
        passwordStore->remove(PasswordStore::key(profile), [this](const QString &error) { reportPasswordError(error); });
    serverPasswords.remove(profile.value("id").toString());
    if (!enteredPassword.isEmpty()) serverPasswords[profile.value("id").toString()] = enteredPassword;
    objects->serverDisconnected(profile.value("id").toString());
    if (index < 0) profiles.append(profile); else profiles[index] = profile;
    settings.setValue("connections/profiles", profiles);
    loadProfiles();
    for (int i = 0; i < objects->topLevelItemCount(); ++i)
        if (objects->topLevelItem(i)->data(0, Qt::UserRole) == profile.value("id"))
            objects->setCurrentItem(objects->topLevelItem(i));
}

// Confirm deletion of an idle profile and remove only its local settings, sessions and vault entry.
void PgAdmMain::removeProfile()
{
    const auto *item = objects->currentItem();
    if (!item) return;
    if (serverInUse(item->data(0, Qt::UserRole).toString())) {
        QMessageBox::information(this, tr("Profiel in gebruik"), tr("Verbreek eerst de verbinding voordat je dit profiel verwijdert."));
        return;
    }
    if (QMessageBox::question(this, tr("Profiel verwijderen"),
        tr("Het lokale profiel '%1' verwijderen?").arg(item->text(0))) != QMessageBox::Yes) return;
    auto profiles = settings.value("connections/profiles").toList();
    const auto id = item->data(0, Qt::UserRole).toString();
    const auto profile = profileById(id);
// Hash the server login identity while deliberately sharing credentials across its databases.
    passwordStore->remove(PasswordStore::key(profile), [this](const QString &error) { reportPasswordError(error); });
    objects->serverDisconnected(id);
    for (const auto &key : sessionProfiles.keys()) {
        if (sessionProfiles.value(key).value("serverId").toString() != id) continue;
        delete sessions.take(key);
        sessionProfiles.remove(key);
        attemptedPasswords.remove(key);
    }
    serverPasswords.remove(id);
    for (int i = profiles.size() - 1; i >= 0; --i)
        if (profiles[i].toMap().value("id").toString() == id) profiles.removeAt(i);
    settings.setValue("connections/profiles", profiles);
    loadProfiles();
}

// Resolve the selected tree node back to its root profile ID, regardless of node depth.
QVariantMap PgAdmMain::selectedProfile() const
{
    const auto *item = objects->currentItem();
    if (!item) return {};
    for (const auto &value : settings.value("connections/profiles").toList())
        if (value.toMap().value("id") == item->data(0, Qt::UserRole)) return value.toMap();
    return {};
}

// Look up a local profile by stable ID; an empty map means the profile was removed.
QVariantMap PgAdmMain::profileById(const QString &id) const
{
    for (const auto &value : settings.value("connections/profiles").toList())
        if (value.toMap().value("id").toString() == id) return value.toMap();
    return {};
}

// Keep the maintenance ID stable and encode other database names without separator collisions.
QString PgAdmMain::connectionId(const QVariantMap &profile, const QString &database)
{
    const auto id = profile.value("id").toString();
    return database == profile.value("database").toString() ? id :
        id + "/" + QString::fromLatin1(database.toUtf8().toBase64(QByteArray::Base64UrlEncoding));
}

// Resolve the selected node to its database connection, falling back to the maintenance database.
QString PgAdmMain::selectedConnectionId() const
{
    const auto profile = selectedProfile();
    const auto database = objects->selectedDatabase();
    return connectionId(profile, database.isEmpty() ? profile.value("database").toString() : database);
}

// Include pending vault reads and all active database sessions when deciding whether a server is idle.
bool PgAdmMain::serverInUse(const QString &id) const
{
    if (roleManager && roleManagerServer == id) return true;
    if (databaseTask && databaseTask->isRunning() && maintenanceServer == id) return true;
    for (const auto &key : sessionProfiles.keys()) {
        if (sessionProfiles.value(key).value("serverId").toString() != id) continue;
        auto *session = sessions.value(key);
        if (pendingPasswordReads.contains(key) || (session && session->state() != PgSession::Disconnected)) return true;
    }
    return false;
}

// Surface a real vault failure without displaying or logging the password.
void PgAdmMain::reportPasswordError(const QString &error)
{
    if (!error.isEmpty()) QMessageBox::warning(this, tr("Wachtwoordopslag"), tr("Wachtwoordopslag is niet gelukt: %1").arg(error));
}

// Connect the selected database or maintenance database without automatically opening an editor.
void PgAdmMain::connectProfile()
{
    const auto profile = selectedProfile();
    const auto database = objects->selectedDatabase();
    connectDatabase(profile, database.isEmpty() ? profile.value("database").toString() : database, false);
}

// Request a query tab for the selected database, connecting first if necessary.
void PgAdmMain::openDatabaseQuery()
{
    const auto profile = selectedProfile();
    const auto database = objects->selectedDatabase();
    if (profile.isEmpty() || database.isEmpty()) return;
    connectDatabase(profile, database, true);
}

// Reuse or create one database session, read its secret asynchronously and route its lifecycle signals.
void PgAdmMain::connectDatabase(const QVariantMap &profile, const QString &database, bool openQuery)
{
    if (profile.isEmpty()) return;
    if (databaseTask && databaseTask->isRunning() && maintenanceServer == profile.value("id").toString() && maintenanceDatabase == database) {
        QMessageBox::information(this, tr("Onderhoudstaak bezig"), tr("Wacht tot restore of verwijderen voor deze database is afgerond."));
        return;
    }
    const auto id = connectionId(profile, database);
    auto *session = sessions.value(id);
    if (session && session->state() == PgSession::Connected) {
        if (openQuery) newQuery(id);
        return;
    }
    if (openQuery) openQueryAfterConnect.insert(id);
    if (pendingPasswordReads.contains(id) || (session && session->state() != PgSession::Disconnected)) return;
    int active = pendingPasswordReads.size();
    for (auto *existing : sessions) if (existing->state() != PgSession::Disconnected) ++active;
    if (active >= 8) {
        if (openQuery) openQueryAfterConnect.remove(id);
        QMessageBox::information(this, tr("Verbindingslimiet"), tr("Verbreek eerst een van de acht actieve verbindingen."));
        return;
    }
    auto runtime = profile;
    runtime["serverId"] = profile.value("id");
    runtime["id"] = id;
    runtime["database"] = database;
    runtime["name"] = profile.value("name").toString() + " / " + database;
    sessionProfiles[id] = runtime;
    if (openQuery) openQueryAfterConnect.insert(id);
    if (!session) {
        session = new PgSession(this);
        sessions.insert(id, session);
        connect(session, &PgSession::stateChanged, this, &PgAdmMain::updateConnectionState);
        connect(session, &PgSession::resultReady, this, [this, session](const QueryResult &result) {
            if (auto *tab = executing.value(session)) tab->appendResult(result);
        });
        connect(session, &PgSession::notice, this, [this, session](const QString &message) {
            if (auto *tab = executing.value(session)) tab->appendMessage(message);
        });
        connect(session, &PgSession::executionFinished, this, [this, session](bool success, const QString &transaction) {
            if (auto *tab = executing.take(session)) tab->finishExecution(success, transaction);
            updateQueryActions();
        });
        connect(session, &PgSession::disconnected, this, [this, id] {
            attemptedPasswords.remove(id);
            const auto serverId = sessionProfiles.value(id).value("serverId").toString();
            if (!serverInUse(serverId)) { objects->serverDisconnected(serverId); serverPasswords.remove(serverId); }
            statusBar()->showMessage(tr("Verbinding gesloten."), 5000);
        });
        connect(session, &PgSession::connected, this, [this, id](const QVariantMap &info) {
            const auto runtime = sessionProfiles.value(id);
            const auto serverId = runtime.value("serverId").toString();
            const auto profile = profileById(serverId);
            const auto password = attemptedPasswords.take(id);
            serverPasswords[serverId] = password;
            if (profile.value("rememberPassword").toBool() && !password.isEmpty())
// Hash the server login identity while deliberately sharing credentials across its databases.
                passwordStore->write(PasswordStore::key(profile), password, [this](const QString &error) { reportPasswordError(error); });
            auto catalogProfile = profile;
            // Use the successfully opened database as the maintenance connection for catalog discovery.
            catalogProfile["database"] = info.value("database");
            objects->serverConnected(catalogProfile, password);
            updateConnectionState();
            if (openQueryAfterConnect.remove(id)) newQuery(id);
            statusBar()->showMessage(tr("Verbonden met %1 op %2:%3 — PostgreSQL %4")
                .arg(info.value("database").toString(), info.value("host").toString(), info.value("port").toString(), info.value("serverVersion").toString()), 10000);
        });
        connect(session, &PgSession::failed, this, [this, id](const QString &message, bool needsPassword) {
            attemptedPasswords.remove(id);
            const auto serverId = sessionProfiles.value(id).value("serverId").toString();
            if (!serverInUse(serverId)) { objects->serverDisconnected(serverId); serverPasswords.remove(serverId); }
            statusBar()->showMessage(tr("Verbinding niet tot stand gekomen of verbroken."), 10000);
            if (needsPassword) promptPassword(id);
            else { openQueryAfterConnect.remove(id); QMessageBox::warning(this, tr("PostgreSQL-verbinding mislukt"), message); }
        });
    }
    if (profile.value("rememberPassword").toBool()) {
        const quint64 read = ++passwordReadSequence;
        pendingPasswordReads.insert(id, read);
        updateConnectionState();
// Hash the server login identity while deliberately sharing credentials across its databases.
        passwordStore->read(PasswordStore::key(profile), [this, id, read](bool found, const QString &password, const QString &error) {
            if (pendingPasswordReads.value(id) != read) return;
            pendingPasswordReads.remove(id);
            reportPasswordError(error);
            const auto serverId = sessionProfiles.value(id).value("serverId").toString();
            openSession(id, found ? password : serverPasswords.value(serverId));
        });
    } else openSession(id, serverPasswords.value(profile.value("id").toString()));
}

// Consume the resolved in-memory secret only if the corresponding session is still disconnected.
void PgAdmMain::openSession(const QString &id, const QString &password)
{
    auto *session = sessions.value(id);
    if (!session || session->state() != PgSession::Disconnected) return;
    attemptedPasswords[id] = password;
    statusBar()->showMessage(tr("Verbinden met %1...").arg(sessionProfiles.value(id).value("name").toString()));
    session->open(sessionProfiles.value(id), password);
}

// Collect a login secret and remember preference locally, then retry the same target connection.
void PgAdmMain::promptPassword(const QString &id)
{
    const auto runtime = sessionProfiles.value(id);
    auto profile = profileById(runtime.value("serverId").toString());
    if (profile.isEmpty()) return;
    QDialog dialog(this);
    dialog.setWindowTitle(tr("PostgreSQL-wachtwoord"));
    auto *layout = new QFormLayout(&dialog);
    auto *label = new QLabel(tr("%1@%2:%3/%4").arg(runtime.value("user").toString(), runtime.value("host").toString(),
        runtime.value("port").toString(), runtime.value("database").toString()), &dialog);
    layout->addRow(label);
    auto *password = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    layout->addRow(tr("Wachtwoord"), password);
    auto *remember = new QCheckBox(tr("Wachtwoord onthouden"), &dialog);
    remember->setChecked(profile.value("rememberPassword").toBool());
    layout->addRow(remember);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) { openQueryAfterConnect.remove(id); return; }
    auto profiles = settings.value("connections/profiles").toList();
    for (auto &value : profiles) {
        auto saved = value.toMap();
        if (saved.value("id") != profile.value("id")) continue;
        saved["rememberPassword"] = remember->isChecked();
        value = saved;
    }
    settings.setValue("connections/profiles", profiles);
// Hash the server login identity while deliberately sharing credentials across its databases.
    if (!remember->isChecked()) passwordStore->remove(PasswordStore::key(profile), [this](const QString &error) { reportPasswordError(error); });
    openSession(id, password->text());
}

// Cancel pending login reads and close either the selected database or all sessions under its server.
void PgAdmMain::disconnectProfile()
{
    const auto serverId = selectedProfile().value("id").toString();
    const auto selected = selectedConnectionId();
    const bool all = objects->selectedDatabase().isEmpty();
    for (const auto &key : sessionProfiles.keys()) {
        if ((all && sessionProfiles.value(key).value("serverId").toString() == serverId) || (!all && key == selected)) {
            pendingPasswordReads.remove(key);
            openQueryAfterConnect.remove(key);
            if (auto *session = sessions.value(key)) session->close();
        }
    }
    if (all) { objects->serverDisconnected(serverId); serverPasswords.remove(serverId); }
    updateConnectionState();
    statusBar()->showMessage(tr("Verbinding wordt gesloten."), 5000);
}

// Derive action availability, server indicators and tab connection choices from current sessions.
void PgAdmMain::updateConnectionState()
{
    const auto *selectedItem = objects->currentItem();
    const auto kind = selectedItem ? selectedItem->data(0, CatalogTree::NodeKind).toInt() : CatalogTree::Info;
    propertiesAction->setEnabled(selectedItem && !selectedItem->isDisabled() &&
        (kind == CatalogTree::Database || kind == CatalogTree::Schema ||
         kind == CatalogTree::Relation || kind == CatalogTree::Column));
    bool ready = false;
    const auto profileId = selectedProfile().value("id").toString();
    for (auto session = sessions.cbegin(); session != sessions.cend(); ++session)
        if (session.value()->state() == PgSession::Connected && sessionProfiles.value(session.key()).value("serverId").toString() == profileId) ready = true;
    const bool taskAvailable = !databaseTask || !databaseTask->isRunning();
    const bool taskNode = kind == CatalogTree::Server || kind == CatalogTree::DatabaseList || kind == CatalogTree::Database;
    const bool allowed = ready && taskAvailable && taskNode && selectedItem && !selectedItem->isDisabled();
    backupAction->setEnabled(allowed);
    restoreAction->setEnabled(allowed);
    createDatabaseAction->setEnabled(allowed);
    rolesAction->setEnabled(ready && taskAvailable);
    const auto selectedDatabase = objects->selectedDatabase();
    dropDatabaseAction->setEnabled(allowed && (selectedDatabase.isEmpty() || DatabaseJob::dropError(selectedProfile(), selectedDatabase).isEmpty()));
    const auto id = selectedConnectionId();
    auto *selected = sessions.value(id);
    const auto state = selected ? selected->state() : PgSession::Disconnected;
    connectAction->setEnabled(!selectedProfile().isEmpty() && selectedItem && !selectedItem->isDisabled() && state == PgSession::Disconnected && !pendingPasswordReads.contains(id));
    databaseQueryAction->setEnabled(!selectedProfile().isEmpty() && !objects->selectedDatabase().isEmpty() &&
        objects->currentItem() && !objects->currentItem()->isDisabled() && state != PgSession::Disconnecting);
    disconnectAction->setEnabled(objects->selectedDatabase().isEmpty() ? serverInUse(selectedProfile().value("id").toString()) :
        pendingPasswordReads.contains(id) || state == PgSession::Connecting || state == PgSession::Connected);
    int active = 0;
    for (auto *session : sessions) if (session->state() == PgSession::Connected) ++active;
    if (state == PgSession::Connected) {
        const auto info = selected->information();
        connectionLabel->setText(tr("%1@%2 / %3 | %4 | %5 actief")
            .arg(info.value("user").toString(), info.value("host").toString(), info.value("database").toString(),
                 info.value("ssl").toBool() ? tr("SSL") : tr("geen SSL"), QString::number(active)));
    } else {
        connectionLabel->setText(tr("%1 | %2 actief").arg(state == PgSession::Connecting ? tr("Verbinden...") :
            state == PgSession::Disconnecting ? tr("Verbreken...") : tr("Niet verbonden"), QString::number(active)));
    }
    for (int i = 0; i < objects->topLevelItemCount(); ++i) {
        auto *item = objects->topLevelItem(i);
        auto *session = sessions.value(item->data(0, Qt::UserRole).toString());
        if (!session || session->state() != PgSession::Connected) {
            for (auto it = sessions.cbegin(); it != sessions.cend(); ++it)
                if (it.value()->state() == PgSession::Connected && sessionProfiles.value(it.key()).value("serverId") == item->data(0, Qt::UserRole)) {
                    session = it.value(); break;
                }
        }
        auto font = item->font(0);
        font.setBold(session && session->state() == PgSession::Connected);
        item->setFont(0, font);
        item->setIcon(0, style()->standardIcon(session && session->state() == PgSession::Connected
            ? QStyle::SP_DialogApplyButton : QStyle::SP_DriveNetIcon));
        if (session && session->state() == PgSession::Connected) {
            const auto info = session->information();
            item->setToolTip(0, tr("Verbonden: %1@%2:%3/%4\nPostgreSQL %5\nBackend PID: %6")
                .arg(info.value("user").toString(), info.value("host").toString(), info.value("port").toString(),
                     info.value("database").toString(), info.value("serverVersion").toString(), info.value("backendPid").toString()));
        } else item->setToolTip(0, tr("Niet verbonden — dubbelklik om te verbinden."));
    }
    QVariantList connectedProfiles;
    for (auto it = sessionProfiles.cbegin(); it != sessionProfiles.cend(); ++it) {
        const auto profile = it.value();
        auto *session = sessions.value(it.key());
        if (session && session->state() == PgSession::Connected) connectedProfiles.append(profile);
    }
    for (int i = 0; i < queries->count(); ++i)
        qobject_cast<QueryTab *>(queries->widget(i))->setConnections(connectedProfiles);
    updateQueryActions();
}

// Capture a reviewed target before opening a modeless task; later tree changes cannot retarget it.
void PgAdmMain::openDatabaseTask(DatabaseJob::Operation operation)
{
    if (databaseTask && databaseTask->isRunning()) {
        databaseTask->raise();
        databaseTask->activateWindow();
        return;
    }
    const auto profile = selectedProfile();
    if (profile.isEmpty()) return;
    const auto id = profile.value("id").toString();
    QString database = operation == DatabaseJob::Create ? QString() : objects->selectedDatabase();
    if (operation != DatabaseJob::Create && database.isEmpty()) {
        auto names = objects->databaseNames(id);
        if (operation == DatabaseJob::Drop) {
            for (int index = names.size() - 1; index >= 0; --index)
                if (!DatabaseJob::dropError(profile, names[index]).isEmpty()) names.removeAt(index);
        }
        if (names.isEmpty()) {
            QMessageBox::information(this, tr("Database selecteren"), tr("Open eerst de databaselijst. Er is nu geen geschikte database geselecteerd."));
            return;
        }
        bool ok = false;
        database = QInputDialog::getItem(this, tr("Database selecteren"), tr("Database:"), names, 0, false, &ok);
        if (!ok) return;
    }
    if (operation == DatabaseJob::Drop) {
        const auto error = DatabaseJob::dropError(profile, database);
        if (!error.isEmpty()) { QMessageBox::information(this, tr("Database verwijderen"), error); return; }
    }
    delete databaseTask;
    auto *dialog = new DatabaseTaskDialog(operation, profile, serverPasswords.value(id), database, this);
    databaseTask = dialog;
    connect(dialog, &DatabaseTaskDialog::startRequested, this, [this, dialog, operation, id, profile] {
        if (roleManager && roleManagerServer == id && roleManager->isApplying()) {
            QMessageBox::information(this, tr("Rollenwijziging bezig"), tr("Wacht tot de rollenwijziging is afgerond."));
            return;
        }
        if (profileById(id) != profile) {
            QMessageBox::information(this, tr("Profiel gewijzigd"), tr("Het profiel is intussen gewijzigd. Sluit dit taakvenster en open de taak opnieuw."));
            return;
        }
        const auto database = dialog->database();
        const bool exclusive = operation == DatabaseJob::Restore || operation == DatabaseJob::Drop;
        if (exclusive && !releaseDatabase(id, database)) return;
        maintenanceServer = id;
        maintenanceDatabase = exclusive ? database : QString();
        dialog->begin();
        updateConnectionState();
    });
    connect(dialog, &DatabaseTaskDialog::completed, this, [this, dialog, id](bool success) {
        objects->setDatabaseBusy(id, dialog->database(), false);
        maintenanceServer.clear();
        maintenanceDatabase.clear();
        objects->refreshDatabases(id);
        updateConnectionState();
        statusBar()->showMessage(success ? tr("Databasetaak voltooid.") : tr("Databasetaak gestopt; zie de loguitvoer."), 7000);
    });
    connect(dialog, &DatabaseTaskDialog::rolesRequested, this, [this, id, profile](const QString &name) {
        if (profileById(id) != profile) {
            QMessageBox::information(this, tr("Profiel gewijzigd"), tr("Het profiel is gewijzigd. Open rollenbeheer opnieuw vanuit de serverboom."));
            return;
        }
        openRoleManager(id, name);
    });
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (databaseTask == dialog) databaseTask = nullptr;
    });
    dialog->show();
}

// Open one captured server context; missing-role suggestions only populate an editable form.
void PgAdmMain::openRoleManager(const QString &profileId, const QString &suggestedRole)
{
    if (databaseTask && databaseTask->isRunning()) {
        databaseTask->raise();
        return;
    }
    const auto profile = profileById(profileId);
    if (profile.isEmpty()) return;
    if (roleManager && roleManagerServer == profileId) {
        roleManager->show();
        roleManager->raise();
        if (!suggestedRole.isEmpty()) roleManager->suggestRole(suggestedRole);
        return;
    }
    if (roleManager) {
        if (!roleManager->mayClose()) return;
        delete roleManager;
    }
    const auto database = selectedProfile().value("id").toString() == profileId ? objects->selectedDatabase() : QString();
    auto *dialog = new RoleManager(profile, serverPasswords.value(profileId), database, suggestedRole, this);
    roleManager = dialog;
    roleManagerServer = profileId;
    connect(dialog, &RoleManager::rolesChanged, this, [this, profileId] {
        objects->refreshDatabases(profileId);
        updateConnectionState();
    });
    connect(dialog, &QObject::destroyed, this, [this, dialog] {
        if (roleManager == dialog) { roleManager = nullptr; roleManagerServer.clear(); }
    });
    dialog->show();
}

// Refuse active user queries, then close only our target sessions and metadata before restore/drop.
bool PgAdmMain::releaseDatabase(const QString &profileId, const QString &database)
{
    QStringList targets;
    for (auto entry = sessionProfiles.cbegin(); entry != sessionProfiles.cend(); ++entry) {
        if (entry.value().value("serverId").toString() != profileId || entry.value().value("database").toString() != database) continue;
        auto *session = sessions.value(entry.key());
        if (session && session->isBusy()) {
            QMessageBox::information(this, tr("Query wordt uitgevoerd"), tr("Annuleer eerst de actieve query op deze database of wacht tot deze klaar is."));
            return false;
        }
        targets.append(entry.key());
    }
    objects->setDatabaseBusy(profileId, database, true);
    objectDetails->clear();
    objects->metadata()->closeDatabase(profileId, database);
    for (const auto &id : targets) {
        pendingPasswordReads.remove(id);
        openQueryAfterConnect.remove(id);
        attemptedPasswords.remove(id);
        delete sessions.take(id);
    }
    updateConnectionState();
    return true;
}

// Inspect only a visible detail pane, using the selected database and in-memory credentials.
void PgAdmMain::updateObjectProperties()
{
    if (!detailsDock->isVisible()) return;
    auto *item = objects->currentItem();
    auto profile = selectedProfile();
    if (!item || item->isDisabled() || profile.isEmpty()) {
        objectDetails->clear();
        return;
    }
    const auto kind = CatalogTree::Kind(item->data(0, CatalogTree::NodeKind).toInt());
    CatalogService::Kind request;
    switch (kind) {
    case CatalogTree::Database: request = CatalogService::DatabaseProperties; break;
    case CatalogTree::Schema: request = CatalogService::SchemaProperties; break;
    case CatalogTree::Relation: request = CatalogService::RelationProperties; break;
    case CatalogTree::Column: request = CatalogService::ColumnProperties; break;
    default:
        objectDetails->clear();
        return;
    }
    profile["database"] = item->data(0, CatalogTree::DatabaseName);
    objectDetails->load(profile, serverPasswords.value(profile.value("id").toString()), request,
        item->data(0, CatalogTree::SchemaName).toString(), item->data(0, CatalogTree::RelationName).toString(),
        item->data(0, CatalogTree::ColumnName).toString());
}

// Allow execution only for an idle target session; expose cancellation for the running tab.
void PgAdmMain::updateQueryActions()
{
    auto *tab = currentQuery();
    auto *session = tab ? sessions.value(tab->connectionId()) : nullptr;
    runAction->setEnabled(tab && !tab->isRunning() && session && session->state() == PgSession::Connected && !session->isBusy());
    stopAction->setEnabled(tab && tab->isRunning());
}

// Resolve the current tab to its session and register result routing before dispatching the selected SQL.
void PgAdmMain::runQuery()
{
    auto *tab = currentQuery();
    auto *session = tab ? sessions.value(tab->connectionId()) : nullptr;
    if (!tab || tab->isRunning() || !session || session->state() != PgSession::Connected || session->isBusy()) return;
    const auto sql = tab->sqlToExecute();
    if (sql.size() > 8 * 1024 * 1024) {
        QMessageBox::warning(this, tr("Script te groot"), tr("De editor voert maximaal 8 miljoen tekens per keer uit."));
        return;
    }
    tab->startExecution();
    executing.insert(session, tab);
    if (!session->execute(sql)) {
        executing.remove(session);
        tab->finishExecution(false, tr("Verbinding is niet beschikbaar"));
    }
    updateQueryActions();
}

// Find the session executing the current tab and request asynchronous PostgreSQL cancellation.
void PgAdmMain::cancelQuery()
{
    for (auto it = executing.cbegin(); it != executing.cend(); ++it)
        if (it.value() == currentQuery()) it.key()->cancel();
}

