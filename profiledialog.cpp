#include "profiledialog.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QSpinBox>
#include <QUuid>

// Edit a local copy of a profile. Only acceptance updates the caller's values;
// password storage and live connection state remain PgAdmMain's responsibility.
bool ProfileDialog::edit(QWidget *parent, bool create, QVariantMap &profile, QString &enteredPassword)
{
    QDialog dialog(parent);
    dialog.setWindowTitle(create ? QObject::tr("Nieuw verbindingsprofiel") : QObject::tr("Verbindingsprofiel wijzigen"));
    auto *layout = new QFormLayout(&dialog);
    auto *name = new QLineEdit(profile.value("name").toString(), &dialog);
    auto *host = new QLineEdit(profile.value("host", "localhost").toString(), &dialog);
    auto *port = new QSpinBox(&dialog);
    port->setRange(1, 65535);
    port->setValue(profile.value("port", 5432).toInt());
    auto *database = new QLineEdit(profile.value("database", "postgres").toString(), &dialog);
    auto *user = new QLineEdit(profile.value("user", "postgres").toString(), &dialog);
    auto *password = new QLineEdit(&dialog);
    password->setEchoMode(QLineEdit::Password);
    password->setPlaceholderText(QObject::tr("Leeg laten om het bestaande wachtwoord te behouden"));
    auto *remember = new QCheckBox(QObject::tr("Wachtwoord onthouden"), &dialog);
    remember->setChecked(profile.value("rememberPassword", false).toBool());
    auto *ssl = new QComboBox(&dialog);
    ssl->addItems({"prefer", "require", "verify-ca", "verify-full", "disable"});
    ssl->setCurrentText(profile.value("sslmode", "prefer").toString());
    auto *timeout = new QSpinBox(&dialog);
    timeout->setRange(2, 120);
    timeout->setValue(profile.value("timeout", 10).toInt());
    timeout->setSuffix(QObject::tr(" seconden"));
    layout->addRow(QObject::tr("Naam"), name);
    layout->addRow(QObject::tr("Host"), host);
    layout->addRow(QObject::tr("Poort"), port);
    layout->addRow(QObject::tr("Onderhoudsdatabase"), database);
    layout->addRow(QObject::tr("Gebruiker"), user);
    layout->addRow(QObject::tr("Wachtwoord"), password);
    layout->addRow(remember);
    layout->addRow(QObject::tr("SSL-modus"), ssl);
    layout->addRow(QObject::tr("Verbindingstime-out"), timeout);
    auto *note = new QLabel(QObject::tr("Gebruik bijvoorbeeld postgres als onderhoudsdatabase; kies daarna een database in de boom.\n"
        "Onthouden gebruikt de systeemkluis. Uitvinken verwijdert het opgeslagen wachtwoord bij opslaan.\n"
        "verify-ca / verify-full gebruiken de libpq-certificaatinstellingen."), &dialog);
    note->setWordWrap(true);
    layout->addRow(note);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
    layout->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [&] {
        if (name->text().trimmed().isEmpty() || host->text().trimmed().isEmpty()
            || database->text().trimmed().isEmpty() || user->text().trimmed().isEmpty()) {
            QMessageBox::warning(&dialog, QObject::tr("Profiel onvolledig"), QObject::tr("Vul alle velden in."));
            return;
        }
        dialog.accept();
    });
    if (dialog.exec() != QDialog::Accepted) return false;
    if (create) profile["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    profile["name"] = name->text().trimmed();
    profile["host"] = host->text().trimmed();
    profile["port"] = port->value();
    profile["database"] = database->text().trimmed();
    profile["user"] = user->text().trimmed();
    profile["sslmode"] = ssl->currentText();
    profile["timeout"] = timeout->value();
    profile["rememberPassword"] = remember->isChecked();
    enteredPassword = password->text();
    return true;
}
