#ifndef ROLEMANAGER_H
#define ROLEMANAGER_H
#include <QDialog>
#include <QHash>
#include <QVariantMap>

class PgSession;
class QCheckBox;
class QCloseEvent;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class QTabWidget;

// goal: Review/create/edit server roles and their direct database privileges.
// pre: GUI-thread use, with an explicitly chosen profile and in-memory login secret.
// post: Reads never change permissions; Apply runs a reviewed, transactional change.
//       Passwords are neither read back nor stored in settings or SQL previews.
// internals: One private PgSession serialises catalog reads, DDL and rollback.
//            RoleSql builds changes against loaded baselines; libpq prepares SCRAM.
class RoleManager : public QDialog
{
    Q_OBJECT
public:
    RoleManager(const QVariantMap &profile, const QString &password, const QString &database,
                const QString &suggestedRole = {}, QWidget *parent = nullptr);
    bool isApplying() const;
    // Focus an existing role, or prepare a new NOLOGIN role without executing DDL.
    void suggestRole(const QString &name);
    // Resolve unsaved form changes and refuse closing in the middle of an apply/rollback.
    bool mayClose();
    void reject() override;
signals:
    void rolesChanged();
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    enum Pending { Idle, ReadRoles, ReadRights, Apply, Rollback };
    void refresh();
    void loadRole(const QString &name);
    void newRole(const QString &name = {});
    void loadRights();
    void receiveRows(const struct QueryResult &result);
    void finishRequest(bool success);
    void apply();
    void updatePreview();
    void updateControls();
    void filter();
    bool discardChanges();
    QVariantMap desiredRole() const;
    QVariantMap desiredRights() const;
    QString changes(QString *error) const;
    QVariantMap profile;
    QString password;
    PgSession *session;
    Pending pending = Idle;
    QVariantList roleRows;
    QStringList databaseNames;
    QHash<QString, QVariantMap> roles;
    QVariantMap originalRole;
    QVariantMap originalRights;
    QString wantedRole;
    QString rightsDatabase;
    QString requestError;
    QString serverNotice;
    bool loading = true;
    bool roleDirty = false;
    bool rightsDirty = false;
    bool rightsReady = false;
    QListWidget *list;
    QListWidget *memberships;
    QLineEdit *search;
    QLineEdit *name;
    QLineEdit *newPassword;
    QLineEdit *validUntil;
    QLineEdit *comment;
    QCheckBox *clearPassword;
    QCheckBox *showSystem;
    QCheckBox *makeOwner;
    QHash<QString, QCheckBox *> attributes;
    QSpinBox *connectionLimit;
    QComboBox *database;
    QTableWidget *rights;
    QTabWidget *tabs;
    QPlainTextEdit *preview;
    QLabel *status;
    QLabel *owner;
    QPushButton *save;
    QPushButton *add;
    QPushButton *reload;
    QWidget *roleForm;
};
#endif
