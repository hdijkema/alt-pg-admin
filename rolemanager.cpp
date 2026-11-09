#include "rolemanager.h"
#include "pgsession.h"
#include "rolesql.h"
#include "sqlhighlighter.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFormLayout>
#include <QFontDatabase>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>

namespace {
bool boolean(const QString &value) { return value == "true" || value == "t"; }
}

// Build a concrete editor; connecting and listing are read-only until the user presses Apply.
RoleManager::RoleManager(const QVariantMap &profile, const QString &password, const QString &initialDatabase,
                         const QString &suggestedRole, QWidget *parent)
    : QDialog(parent), profile(profile), password(password), session(new PgSession(this)), wantedRole(suggestedRole)
{
    setObjectName("roleManager");
    setWindowTitle(tr("Rollen en databaserechten"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1080, 730);
    auto *layout = new QVBoxLayout(this);
    auto *server = new QLabel(tr("%1:%2 | Beheergebruiker: %3 | Rollen gelden voor alle databases op deze server.")
        .arg(profile.value("host").toString(), profile.value("port", 5432).toString(), profile.value("user").toString()), this);
    server->setTextFormat(Qt::PlainText);
    server->setWordWrap(true);
    layout->addWidget(server);
    auto *split = new QSplitter(this);
    auto *left = new QWidget(split);
    auto *leftLayout = new QVBoxLayout(left);
    search = new QLineEdit(left);
    search->setPlaceholderText(tr("Zoek rol..."));
    leftLayout->addWidget(search);
    showSystem = new QCheckBox(tr("Systeemrollen tonen"), left);
    leftLayout->addWidget(showSystem);
    list = new QListWidget(left);
    list->setObjectName("serverRoles");
    leftLayout->addWidget(list, 1);
    add = new QPushButton(tr("Nieuwe rol..."), left);
    add->setObjectName("newRoleButton");
    reload = new QPushButton(tr("Vernieuwen"), left);
    leftLayout->addWidget(add);
    leftLayout->addWidget(reload);
    auto *right = new QWidget(split);
    auto *rightLayout = new QVBoxLayout(right);
    auto *vertical = new QSplitter(Qt::Vertical, right);
    tabs = new QTabWidget(vertical);
    tabs->setObjectName("roleTabs");
    roleForm = new QWidget(tabs);
    auto *formLayout = new QVBoxLayout(roleForm);
    auto *fields = new QFormLayout;
    name = new QLineEdit(roleForm);
    name->setObjectName("roleName");
    fields->addRow(tr("Rolnaam"), name);
    newPassword = new QLineEdit(roleForm);
    newPassword->setObjectName("rolePassword");
    newPassword->setEchoMode(QLineEdit::Password);
    newPassword->setPlaceholderText(tr("Leeg laten houdt een bestaand wachtwoord intact"));
    fields->addRow(tr("Nieuw wachtwoord"), newPassword);
    clearPassword = new QCheckBox(tr("Wachtwoord verwijderen"), roleForm);
    fields->addRow(clearPassword);
    connectionLimit = new QSpinBox(roleForm);
    connectionLimit->setRange(-1, 2147483647);
    connectionLimit->setSpecialValueText(tr("Onbeperkt"));
    fields->addRow(tr("Verbindingslimiet"), connectionLimit);
    validUntil = new QLineEdit(roleForm);
    validUntil->setPlaceholderText(tr("Geen vervaldatum, of PostgreSQL timestamp"));
    fields->addRow(tr("Wachtwoord geldig tot"), validUntil);
    comment = new QLineEdit(roleForm);
    fields->addRow(tr("Commentaar"), comment);
    formLayout->addLayout(fields);
    auto *flags = new QGridLayout;
    const QList<QPair<QString, QString>> labels = {{"login",tr("LOGIN")},{"inherit",tr("INHERIT")},
        {"createdb",tr("Databases aanmaken")},{"createrole",tr("Rollen beheren")},{"superuser",tr("SUPERUSER")},
        {"replication",tr("REPLICATION")},{"bypassrls",tr("BYPASSRLS")}};
    for (int index = 0; index < labels.size(); ++index) {
        auto *check = new QCheckBox(labels[index].second, roleForm);
        check->setObjectName("role_" + labels[index].first);
        attributes.insert(labels[index].first, check);
        flags->addWidget(check, index / 3, index % 3);
    }
    formLayout->addLayout(flags);
    formLayout->addWidget(new QLabel(tr("Lid van rollen — opties van ongewijzigde lidmaatschappen blijven behouden:"), roleForm));
    memberships = new QListWidget(roleForm);
    memberships->setObjectName("roleMemberships");
    formLayout->addWidget(memberships, 1);
    auto *hint = new QLabel(tr("Een nieuwe eigenaarsrol voor een restore kan NOLOGIN blijven. "
        "Wijzig je het wachtwoord van de verbindingsgebruiker, werk dan ook het verbindingsprofiel bij."), roleForm);
    hint->setWordWrap(true);
    formLayout->addWidget(hint);
    tabs->addTab(roleForm, tr("Rol en lidmaatschappen"));
    auto *dbPage = new QWidget(tabs);
    auto *dbLayout = new QVBoxLayout(dbPage);
    database = new QComboBox(dbPage);
    database->setObjectName("roleDatabase");
    database->setProperty("initialDatabase", initialDatabase.isEmpty() ? profile.value("database").toString() : initialDatabase);
    dbLayout->addWidget(database);
    owner = new QLabel(dbPage);
    owner->setTextFormat(Qt::PlainText);
    dbLayout->addWidget(owner);
    rights = new QTableWidget(3, 4, dbPage);
    rights->setObjectName("databaseRoleRights");
    rights->setHorizontalHeaderLabels({tr("Privilege"), tr("Directe GRANT"), tr("GRANT OPTION"), tr("Effectief")});
    rights->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    rights->verticalHeader()->hide();
    rights->setEditTriggers(QAbstractItemView::NoEditTriggers);
    const QStringList privileges{"CONNECT", "CREATE", "TEMPORARY"};
    for (int row = 0; row < privileges.size(); ++row) {
        rights->setItem(row, 0, new QTableWidgetItem(privileges[row]));
        for (int column = 1; column < 3; ++column) {
            auto *item = new QTableWidgetItem;
            item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
            rights->setItem(row, column, item);
        }
        rights->setItem(row, 3, new QTableWidgetItem);
    }
    dbLayout->addWidget(rights, 1);
    makeOwner = new QCheckBox(tr("Database-eigenaar wijzigen naar de geselecteerde rol"), dbPage);
    makeOwner->setObjectName("roleMakeDatabaseOwner");
    dbLayout->addWidget(makeOwner);
    auto *dbHint = new QLabel(tr("Dit beheert directe databaseprivileges, geen tabelrechten. PUBLIC, lidmaatschappen "
        "en eigendom kunnen effectieve rechten geven, ook zonder directe GRANT."), dbPage);
    dbHint->setWordWrap(true);
    dbLayout->addWidget(dbHint);
    tabs->addTab(dbPage, tr("Databaserechten"));
    preview = new QPlainTextEdit(vertical);
    preview->setObjectName("roleSqlPreview");
    preview->setReadOnly(true);
    preview->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new SqlHighlighter(preview->document());
    vertical->setSizes({430, 140});
    rightLayout->addWidget(vertical, 1);
    status = new QLabel(tr("Verbinden..."), right);
    status->setObjectName("roleStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    rightLayout->addWidget(status);
    auto *buttons = new QHBoxLayout;
    buttons->addStretch();
    save = new QPushButton(tr("Toepassen"), right);
    save->setObjectName("roleApplyButton");
    auto *close = new QPushButton(tr("Sluiten"), right);
    buttons->addWidget(save);
    buttons->addWidget(close);
    rightLayout->addLayout(buttons);
    split->setSizes({230, 800});
    layout->addWidget(split, 1);
    auto editedRole = [this] {
        if (loading) return;
        roleDirty = true;
        updatePreview();
    };
    for (auto *field : {name, newPassword, validUntil, comment}) connect(field, &QLineEdit::textChanged, this, editedRole);
    for (auto *check : attributes) connect(check, &QCheckBox::toggled, this, editedRole);
    connect(connectionLimit, &QSpinBox::valueChanged, this, editedRole);
    connect(memberships, &QListWidget::itemChanged, this, editedRole);
    connect(clearPassword, &QCheckBox::toggled, this, [this, editedRole](bool clear) {
        newPassword->setEnabled(!clear);
        if (clear) newPassword->clear();
        editedRole();
    });
    connect(makeOwner, &QCheckBox::toggled, this, [this] { if (!loading) { rightsDirty = true; updatePreview(); } });
    connect(rights, &QTableWidget::itemChanged, this, [this](QTableWidgetItem *item) {
        if (loading || item->column() < 1 || item->column() > 2) return;
        const QSignalBlocker block(rights);
        if (item->column() == 1 && item->checkState() == Qt::Unchecked) rights->item(item->row(), 2)->setCheckState(Qt::Unchecked);
        if (item->column() == 2 && item->checkState() == Qt::Checked) rights->item(item->row(), 1)->setCheckState(Qt::Checked);
        rightsDirty = true;
        updatePreview();
    });
    connect(search, &QLineEdit::textChanged, this, &RoleManager::filter);
    connect(showSystem, &QCheckBox::toggled, this, &RoleManager::filter);
    connect(list, &QListWidget::currentItemChanged, this, [this](QListWidgetItem *current, QListWidgetItem *previous) {
        if (loading || !current) return;
        if (!discardChanges()) { const QSignalBlocker block(list); list->setCurrentItem(previous); return; }
        loadRole(current->text());
    });
    connect(database, &QComboBox::currentTextChanged, this, [this] {
        if (loading) return;
        if (rightsDirty && QMessageBox::question(this, tr("Niet opgeslagen"), tr("Wijzigingen aan databaserechten verwerpen?"),
            QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Discard) {
            const QSignalBlocker block(database);
            database->setCurrentText(rightsDatabase);
            return;
        }
        loadRights();
    });
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (loading) return;
        if ((index == 1 && roleDirty) || (index == 0 && rightsDirty)) {
            const QSignalBlocker block(tabs);
            tabs->setCurrentIndex(1 - index);
            status->setText(tr("Pas eerst de wijzigingen op dit tabblad toe, of vernieuw om ze te verwerpen."));
            return;
        }
        updatePreview();
    });
    connect(add, &QPushButton::clicked, this, [this] { if (discardChanges()) newRole(); });
    connect(reload, &QPushButton::clicked, this, [this] { if (discardChanges()) refresh(); });
    connect(close, &QPushButton::clicked, this, &RoleManager::reject);
    connect(save, &QPushButton::clicked, this, &RoleManager::apply);
    connect(session, &PgSession::connected, this, &RoleManager::refresh);
    connect(session, &PgSession::resultReady, this, &RoleManager::receiveRows);
    connect(session, &PgSession::notice, this, [this](const QString &message) { serverNotice = message.left(2000); });
    connect(session, &PgSession::executionFinished, this, [this](bool success, const QString &) { finishRequest(success); });
    connect(session, &PgSession::failed, this, [this](const QString &message, bool) {
        pending = Idle;
        loading = false;
        status->setText(tr("Verbinding mislukt: %1").arg(message));
        updateControls();
    });
    updateControls();
    session->open(profile, password);
}

// Remember a requested missing-role name; merely selecting/preparing it performs no DDL.
void RoleManager::suggestRole(const QString &role)
{
    if (pending != Idle) { wantedRole = role; return; }
    if (roles.contains(role)) {
        const auto items = list->findItems(role, Qt::MatchExactly);
        if (!items.isEmpty()) { items.first()->setHidden(false); list->setCurrentItem(items.first()); }
    } else if (discardChanges()) newRole(role);
}

// Role attributes and database grants use private transactions, not a user's query session.
bool RoleManager::isApplying() const { return pending == Apply || pending == Rollback; }

// Read public role metadata and database names in one ordered script.
void RoleManager::refresh()
{
    if (pending != Idle) return;
    if (session->state() == PgSession::Disconnected) { session->open(profile, password); return; }
    if (session->state() != PgSession::Connected) return;
    if (wantedRole.isEmpty()) wantedRole = name->text();
    roleRows.clear();
    databaseNames.clear();
    requestError.clear();
    pending = ReadRoles;
    loading = true;
    status->setText(tr("Rollen ophalen..."));
    updateControls();
    session->execute(RoleSql::rolesQuery() + '\n' + RoleSql::databasesQuery());
}

// Decode bounded result rows into explicit role/ACL state; password fields are never queried.
void RoleManager::receiveRows(const QueryResult &result)
{
    if (result.error) requestError = result.status;
    if (result.truncated) requestError = tr("De rol- of databaselijst is te groot om volledig te bewerken.");
    if (pending != ReadRoles && pending != ReadRights) return;
    for (const auto &cells : result.rows) {
        QVariantMap row;
        for (int column = 0; column < cells.size(); ++column) row[result.columns.value(column)] = cells[column].text;
        if (pending == ReadRoles && result.number == 2) { databaseNames.append(row.value("name").toString()); continue; }
        if (pending == ReadRoles) {
            for (const auto &key : attributes.keys()) row[key] = boolean(row.value(key).toString());
            row["connection_limit"] = row.value("connection_limit").toInt();
            QStringList parents;
            for (const auto &parent : QJsonDocument::fromJson(row.value("memberships").toString().toUtf8()).array()) parents.append(parent.toString());
            parents.removeDuplicates();
            row["memberships"] = parents;
            roleRows.append(row);
        } else {
            const auto privilege = row.value("privilege").toString();
            originalRights[privilege] = boolean(row.value("direct").toString());
            originalRights[privilege + "_grant"] = boolean(row.value("grantable").toString());
            originalRights[privilege + "_effective"] = boolean(row.value("effective").toString());
            originalRights["owner"] = row.value("owner");
        }
    }
}

// Finish one phase; failed writes explicitly roll back before controls become editable again.
void RoleManager::finishRequest(bool success)
{
    const auto completed = pending;
    pending = Idle;
    loading = false;
    if (completed == Rollback) {
        status->setText(tr("Wijzigingen niet opgeslagen: %1").arg(requestError));
        updateControls();
        return;
    }
    if (!success || !requestError.isEmpty()) {
        if (requestError.isEmpty()) requestError = tr("De server heeft de bewerking geweigerd.");
        if (completed == Apply && session->state() == PgSession::Connected) {
            pending = Rollback;
            updateControls();
            session->execute("ROLLBACK;");
        } else {
            status->setText(requestError);
            updateControls();
        }
        return;
    }
    if (completed == Apply) {
        roleDirty = rightsDirty = false;
        newPassword->clear();
        clearPassword->setChecked(false);
        roleDirty = false;
        wantedRole = desiredRole().value("name").toString();
        status->setText(serverNotice.isEmpty() ? tr("Wijzigingen opgeslagen.") : serverNotice);
        emit rolesChanged();
        refresh();
    } else if (completed == ReadRoles) {
        loading = true;
        roles.clear();
        list->clear();
        for (const auto &entry : roleRows) {
            const auto row = entry.toMap();
            roles.insert(row.value("name").toString(), row);
            list->addItem(row.value("name").toString());
        }
        const auto previousDatabase = database->currentText();
        database->clear();
        database->addItems(databaseNames);
        database->setCurrentText(previousDatabase.isEmpty() ? database->property("initialDatabase").toString() : previousDatabase);
        const auto requested = wantedRole;
        wantedRole.clear();
        loading = false;
        roleDirty = rightsDirty = false;
        filter();
        if (roles.contains(requested)) {
            const QSignalBlocker block(list);
            list->setCurrentItem(list->findItems(requested, Qt::MatchExactly).first());
            loadRole(requested);
        } else newRole(requested);
    } else if (completed == ReadRights) {
        loading = true;
        rightsReady = !originalRights.isEmpty();
        owner->setText(tr("Huidige database-eigenaar: %1").arg(originalRights.value("owner").toString()));
        for (int row = 0; row < rights->rowCount(); ++row) {
            const auto privilege = rights->item(row, 0)->text();
            rights->item(row, 1)->setCheckState(originalRights.value(privilege).toBool() ? Qt::Checked : Qt::Unchecked);
            rights->item(row, 2)->setCheckState(originalRights.value(privilege + "_grant").toBool() ? Qt::Checked : Qt::Unchecked);
            rights->item(row, 3)->setText(originalRights.value(privilege + "_effective").toBool() ? tr("Ja") : tr("Nee"));
        }
        makeOwner->setChecked(false);
        rightsDirty = false;
        loading = false;
        status->setText(rightsReady ? (serverNotice.isEmpty() ? tr("Gereed.") : serverNotice) : tr("Rol of database bestaat niet meer; vernieuw de lijst."));
        updatePreview();
        if (!wantedRole.isEmpty()) { const auto next = wantedRole; wantedRole.clear(); suggestRole(next); }
    }
}

// Replace the form from one loaded baseline, retaining unrelated membership option values on save.
void RoleManager::loadRole(const QString &role)
{
    if (!roles.contains(role)) return;
    loading = true;
    originalRole = roles.value(role);
    name->setText(role);
    name->setReadOnly(true);
    newPassword->clear();
    clearPassword->setChecked(false);
    connectionLimit->setValue(originalRole.value("connection_limit").toInt());
    validUntil->setText(originalRole.value("valid_until").toString());
    comment->setText(originalRole.value("comment").toString());
    for (auto key = attributes.begin(); key != attributes.end(); ++key) key.value()->setChecked(originalRole.value(key.key()).toBool());
    memberships->clear();
    for (const auto &entry : roleRows) {
        const auto parent = entry.toMap().value("name").toString();
        if (parent == role) continue;
        auto *item = new QListWidgetItem(parent, memberships);
        item->setCheckState(originalRole.value("memberships").toStringList().contains(parent) ? Qt::Checked : Qt::Unchecked);
    }
    roleDirty = rightsDirty = false;
    loading = false;
    tabs->setTabEnabled(1, true);
    loadRights();
}

// Prepare a minimal NOLOGIN role. It is not created until the SQL preview is applied.
void RoleManager::newRole(const QString &role)
{
    loading = true;
    originalRole.clear();
    originalRights.clear();
    rightsReady = false;
    { const QSignalBlocker block(list); list->setCurrentItem(nullptr); }
    name->setReadOnly(false);
    name->setText(role);
    newPassword->clear();
    clearPassword->setChecked(false);
    validUntil->clear();
    comment->clear();
    connectionLimit->setValue(-1);
    for (auto key = attributes.begin(); key != attributes.end(); ++key) key.value()->setChecked(key.key() == "inherit");
    memberships->clear();
    for (const auto &entry : roleRows) {
        auto *item = new QListWidgetItem(entry.toMap().value("name").toString(), memberships);
        item->setCheckState(Qt::Unchecked);
    }
    tabs->setCurrentIndex(0);
    tabs->setTabEnabled(1, false);
    roleDirty = rightsDirty = false;
    loading = false;
    status->setText(tr("Nieuwe rol — standaard NOLOGIN, zonder beheerdersrechten."));
    updatePreview();
}

// Read direct and effective privileges for one existing role/database pair.
void RoleManager::loadRights()
{
    if (pending != Idle || originalRole.isEmpty() || database->currentText().isEmpty()) { updatePreview(); return; }
    rightsDatabase = database->currentText();
    originalRights.clear();
    rightsReady = false;
    requestError.clear();
    pending = ReadRights;
    updateControls();
    session->execute(RoleSql::databaseRightsQuery(originalRole.value("name").toString(), rightsDatabase));
}

// Collect only public editable values; the password remains a separate transient field.
QVariantMap RoleManager::desiredRole() const
{
    QVariantMap values{{"name",originalRole.isEmpty() ? name->text().trimmed() : name->text()},{"connection_limit",connectionLimit->value()},
        {"valid_until",validUntil->text()},{"comment",comment->text()},{"clear_password",clearPassword->isChecked()}};
    for (auto key = attributes.cbegin(); key != attributes.cend(); ++key) values[key.key()] = key.value()->isChecked();
    QStringList parents;
    for (int row = 0; row < memberships->count(); ++row)
        if (memberships->item(row)->checkState() == Qt::Checked) parents.append(memberships->item(row)->text());
    values["memberships"] = parents;
    return values;
}

// Keep direct privileges and grant options distinct from the read-only effective column.
QVariantMap RoleManager::desiredRights() const
{
    QVariantMap values;
    for (int row = 0; row < rights->rowCount(); ++row) {
        const auto privilege = rights->item(row, 0)->text();
        values[privilege] = rights->item(row, 1)->checkState() == Qt::Checked;
        values[privilege + "_grant"] = rights->item(row, 2)->checkState() == Qt::Checked;
    }
    return values;
}

// Build the active tab's exact SQL; database grants are unavailable until their baseline loads.
QString RoleManager::changes(QString *error) const
{
    error->clear();
    if (tabs->currentIndex() == 0) return RoleSql::roleChange(desiredRole(), originalRole, error);
    if (!rightsReady) { *error = tr("Laad eerst de databaserechten."); return {}; }
    return RoleSql::databaseChange(name->text(), rightsDatabase, desiredRights(), originalRights, makeOwner->isChecked(), error);
}

// Show a password-free preview; an optional password is hashed only inside PgConnection.
void RoleManager::updatePreview()
{
    if (loading) return;
    QString dirtyError;
    if (roleDirty && !originalRole.isEmpty())
        roleDirty = !RoleSql::roleChange(desiredRole(), originalRole, &dirtyError).isEmpty() ||
            !dirtyError.isEmpty() || !newPassword->text().isEmpty();
    if (rightsDirty && rightsReady)
        rightsDirty = !RoleSql::databaseChange(name->text(), rightsDatabase, desiredRights(), originalRights,
            makeOwner->isChecked(), &dirtyError).isEmpty() || !dirtyError.isEmpty();
    QString error;
    auto sql = changes(&error);
    if (tabs->currentIndex() == 0 && !newPassword->text().isEmpty())
        sql += "\n-- Nieuw wachtwoord: SCRAM-verifier wordt lokaal voorbereid; het wachtwoord wordt niet getoond.";
    preview->setPlainText(error.isEmpty() ? (sql.isEmpty() ? tr("-- Geen wijzigingen.") : sql) : "-- " + error);
    updateControls();
}

// Avoid retargeting forms during asynchronous reads/writes and protect builtin pg_ roles.
void RoleManager::updateControls()
{
    const bool idle = pending == Idle && session->state() == PgSession::Connected;
    const bool editable = originalRole.isEmpty() || !name->text().startsWith("pg_");
    list->setEnabled(idle);
    database->setEnabled(idle);
    add->setEnabled(idle);
    reload->setEnabled(pending == Idle && session->state() != PgSession::Connecting);
    roleForm->setEnabled(idle && editable);
    rights->setEnabled(idle && editable && rightsReady);
    makeOwner->setEnabled(idle && editable && rightsReady);
    QString error;
    const auto sql = changes(&error);
    const bool passwordChange = tabs->currentIndex() == 0 && !newPassword->text().isEmpty() && !newPassword->text().contains(QChar(0));
    const bool validPassword = tabs->currentIndex() != 0 || !newPassword->text().contains(QChar(0));
    save->setEnabled(idle && editable && error.isEmpty() && validPassword && (!sql.isEmpty() || passwordChange));
}

// Apply only the current tab, using a private transaction and preserving the other form's edits.
void RoleManager::apply()
{
    if (!save->isEnabled()) return;
    QString error;
    const auto sql = changes(&error);
    if (!error.isEmpty()) { status->setText(error); return; }
    if ((tabs->currentIndex() == 0 && rightsDirty) || (tabs->currentIndex() == 1 && roleDirty)) {
        QMessageBox::information(this, tr("Niet opgeslagen"), tr("Pas eerst de wijzigingen op het andere tabblad toe of vernieuw om ze te verwerpen."));
        return;
    }
    requestError.clear();
    serverNotice.clear();
    pending = Apply;
    updateControls();
    status->setText(tr("Wijzigingen uitvoeren..."));
    const bool hasPassword = tabs->currentIndex() == 0 && !newPassword->text().isEmpty();
    const bool accepted = hasPassword ? session->executeRoleChange(sql, desiredRole().value("name").toString(), newPassword->text()) :
        session->execute("BEGIN;\n" + sql + "\nCOMMIT;");
    if (!accepted) { pending = Idle; status->setText(tr("De verbinding is niet beschikbaar.")); updateControls(); }
}

// Search only hides list entries; the explicit current role name remains visible in the form.
void RoleManager::filter()
{
    for (int row = 0; row < list->count(); ++row) {
        const auto text = list->item(row)->text();
        list->item(row)->setHidden((!showSystem->isChecked() && text.startsWith("pg_")) || !text.contains(search->text(), Qt::CaseInsensitive));
    }
}

// Ask before discarding actual form edits, without starting an implicit save or changing targets.
bool RoleManager::discardChanges()
{
    if (!roleDirty && !rightsDirty) return true;
    return QMessageBox::question(this, tr("Niet opgeslagen wijzigingen"), tr("De wijzigingen aan rol of databaserechten verwerpen?"),
        QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel) == QMessageBox::Discard;
}

// Keep the administrator connection alive through a write/rollback; otherwise resolve unsaved inputs.
bool RoleManager::mayClose()
{
    if (isApplying()) { QMessageBox::information(this, tr("Wijzigingen bezig"), tr("Wacht tot toepassen of terugdraaien is afgerond.")); return false; }
    return discardChanges();
}

void RoleManager::reject() { if (mayClose()) QDialog::reject(); }
void RoleManager::closeEvent(QCloseEvent *event) { if (mayClose()) event->accept(); else event->ignore(); }
