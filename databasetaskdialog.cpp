#include "databasetaskdialog.h"
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QRegularExpression>
#include <QPushButton>
#include <QTextCursor>
#include <QTimer>
#include <QVBoxLayout>

// Build one concrete task form and keep its host/database summary independent of tree selection.
DatabaseTaskDialog::DatabaseTaskDialog(DatabaseJob::Operation operation, const QVariantMap &profile,
                                     const QString &password, const QString &database, QWidget *parent)
    : QDialog(parent), operation(operation), profile(profile), password(password), target(database), job(new DatabaseJob(this))
{
    setObjectName("databaseTaskDialog");
    setAttribute(Qt::WA_DeleteOnClose);
    resize(820, 560);
    const QStringList titles{tr("Databasebackup"), tr("Database herstellen"), tr("Database aanmaken"), tr("Database verwijderen")};
    setWindowTitle(titles.at(operation));
    auto *layout = new QVBoxLayout(this);
    auto *summary = new QLabel(tr("Server: %1:%2 | Gebruiker: %3\nDatabase: %4")
        .arg(profile.value("host").toString(), profile.value("port", 5432).toString(), profile.value("user").toString(), database), this);
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    layout->addWidget(summary);
    auto *form = new QFormLayout;
    name = new QLineEdit(database, this);
    name->setObjectName("databaseTaskName");
    name->setReadOnly(operation != DatabaseJob::Create);
    form->addRow(tr("Databasenaam"), name);
    file = new QLineEdit(this);
    file->setObjectName("databaseTaskFile");
    file->setReadOnly(true);
    browseButton = new QPushButton(tr("Bladeren..."), this);
    browseButton->setObjectName("databaseTaskBrowse");
    auto *files = new QHBoxLayout;
    files->addWidget(file);
    files->addWidget(browseButton);
    const bool transfer = operation == DatabaseJob::Backup || operation == DatabaseJob::Restore;
    if (transfer) form->addRow(tr("Bestand"), files);
    else {
        file->hide();
        browseButton->hide();
        delete files;
    }
    format = new QComboBox(this);
    format->setObjectName("databaseBackupFormat");
    format->addItems({tr("PostgreSQL-archief (.backup)"), tr("SQL-script (.sql)")});
    if (operation == DatabaseJob::Backup) form->addRow(tr("Formaat"), format);
    else format->hide();
    clean = new QCheckBox(tr("Bestaande objecten uit het archief eerst verwijderen"), this);
    clean->setObjectName("databaseRestoreClean");
    skipOwners = new QCheckBox(tr("Eigenaren en privileges uit het archief overslaan"), this);
    skipOwners->setObjectName("databaseRestoreSkipOwners");
    skipOwners->setChecked(true);
    if (operation == DatabaseJob::Restore) {
        form->addRow(clean);
        form->addRow(skipOwners);
    } else {
        clean->hide();
        skipOwners->hide();
    }
    confirmation = new QLineEdit(this);
    confirmation->setObjectName("databaseDropConfirmation");
    if (operation == DatabaseJob::Drop) {
        confirmation->setPlaceholderText(tr("Typ de volledige databasenaam ter bevestiging"));
        form->addRow(tr("Bevestiging"), confirmation);
    } else confirmation->hide();
    layout->addLayout(form);
    QString note;
    if (operation == DatabaseJob::Backup)
        note = tr("Backup van de volledige database, inclusief data. Een bestaand bestand wordt pas na een geslaagde backup vervangen.");
    else if (operation == DatabaseJob::Restore)
        note = tr("Herstel naar bovenstaande database, bij voorkeur een lege database. Eigen verbindingen worden gesloten; open transacties worden teruggedraaid. "
            "Archieven worden in één transactie hersteld. SQL-bestanden worden door psql uitgevoerd; commando's in het script zijn bepalend. Gebruik een vertrouwde backup.");
    else if (operation == DatabaseJob::Create)
        note = tr("Maak een lege UTF-8-database op basis van template0. De ingelogde gebruiker wordt eigenaar.");
    else note = tr("Verwijder deze database en alle inhoud definitief. Eigen verbindingen worden gesloten en open transacties teruggedraaid. Andere clients worden niet geforceerd verbroken.");
    auto *description = new QLabel(note, this);
    description->setWordWrap(true);
    layout->addWidget(description);
    status = new QLabel(tr("Gereed om te starten."), this);
    status->setObjectName("databaseTaskStatus");
    status->setTextFormat(Qt::PlainText);
    status->setWordWrap(true);
    layout->addWidget(status);
    duration = new QLabel(this);
    duration->setObjectName("databaseTaskDuration");
    layout->addWidget(duration);
    durationTimer = new QTimer(this);
    durationTimer->setInterval(250);
    connect(durationTimer, &QTimer::timeout, this, &DatabaseTaskDialog::updateDuration);
    updateDuration();
    progress = new QProgressBar(this);
    progress->setRange(0, 1);
    progress->setValue(0);
    progress->setTextVisible(false);
    layout->addWidget(progress);
    log = new QPlainTextEdit(this);
    log->setObjectName("databaseTaskLog");
    log->setReadOnly(true);
    log->setMaximumBlockCount(2000);
    layout->addWidget(log, 1);
    auto *buttons = new QHBoxLayout;
    roles = new QPushButton(tr("Rollen beheren..."), this);
    roles->setObjectName("restoreRolesButton");
    if (operation == DatabaseJob::Restore) buttons->addWidget(roles);
    else roles->hide();
    buttons->addStretch();
    start = new QPushButton(tr("Starten"), this);
    start->setObjectName("databaseTaskStart");
    close = new QPushButton(tr("Sluiten"), this);
    close->setObjectName("databaseTaskClose");
    buttons->addWidget(start);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(start, &QPushButton::clicked, this, &DatabaseTaskDialog::startRequested);
    connect(close, &QPushButton::clicked, this, &DatabaseTaskDialog::reject);
    connect(roles, &QPushButton::clicked, this, [this] {
        const QRegularExpression expression("(?:role|rol) \"([^\"]+)\"", QRegularExpression::CaseInsensitiveOption);
        const auto match = expression.match(log->toPlainText());
        emit rolesRequested(match.hasMatch() ? match.captured(1) : QString());
    });
    connect(browseButton, &QPushButton::clicked, this, &DatabaseTaskDialog::browse);
    connect(name, &QLineEdit::textChanged, this, &DatabaseTaskDialog::updateControls);
    connect(file, &QLineEdit::textChanged, this, &DatabaseTaskDialog::updateControls);
    connect(confirmation, &QLineEdit::textChanged, this, &DatabaseTaskDialog::updateControls);
    connect(format, &QComboBox::currentIndexChanged, this, [this] { file->clear(); });
    connect(job, &DatabaseJob::output, this, &DatabaseTaskDialog::appendLog);
    connect(job, &DatabaseJob::finished, this, [this](bool success, const QString &message) {
        durationTimer->stop();
        updateDuration();
        status->setText(message);
        progress->setRange(0, 1);
        progress->setValue(success ? 1 : 0);
        this->password.clear();
        updateControls();
        emit completed(success);
    });
    updateControls();
}

// The create field is editable until Start; all other operations retain their captured target.
QString DatabaseTaskDialog::database() const
{
    return operation == DatabaseJob::Create ? name->text().trimmed() : target;
}

// Select a reviewed input/output file; the path cannot be edited after overwrite confirmation.
void DatabaseTaskDialog::browse()
{
    QString path;
    if (operation == DatabaseJob::Backup) {
        const bool plain = format->currentIndex() == 1;
        path = QFileDialog::getSaveFileName(this, tr("Backup opslaan"), plain ? "database.sql" : "database.backup",
            plain ? tr("SQL-script (*.sql)") : tr("PostgreSQL-archief (*.backup)"));
    } else path = QFileDialog::getOpenFileName(this, tr("Backup herstellen"), QString(),
        tr("PostgreSQL-backups (*.backup *.dump *.tar *.sql);;Alle bestanden (*)"));
    if (!path.isEmpty()) file->setText(path);
}

// Enable only meaningful controls; a finished dialog cannot repeat a destructive operation.
void DatabaseTaskDialog::updateControls()
{
    const bool busy = job->isRunning();
    bool valid = DatabaseJob::nameError(database()).isEmpty();
    if (operation == DatabaseJob::Backup) valid = valid && !file->text().isEmpty();
    if (operation == DatabaseJob::Restore) {
        valid = valid && QFileInfo(file->text()).isFile();
        const bool archive = DatabaseJob::isArchive(file->text());
        clean->setEnabled(!started && archive);
        skipOwners->setEnabled(!started && archive);
        if (!archive) clean->setChecked(false);
    }
    if (operation == DatabaseJob::Drop)
        valid = valid && confirmation->text() == target && DatabaseJob::dropError(profile, target).isEmpty();
    name->setEnabled(!started);
    confirmation->setEnabled(!started);
    browseButton->setEnabled(!started);
    format->setEnabled(!started);
    start->setEnabled(!started && valid);
    close->setText(busy ? tr("Annuleren") : tr("Sluiten"));
    roles->setEnabled(!busy);
}

// The owner calls this only after reviewing busy sessions and releasing its own target connections.
void DatabaseTaskDialog::begin()
{
    if (started || !start->isEnabled()) return;
    started = true;
    elapsed.start();
    durationTimer->start();
    updateDuration();
    status->setText(tr("Bezig..."));
    progress->setRange(0, 0);
    if (operation == DatabaseJob::Backup) job->backup(profile, password, database(), file->text(), format->currentIndex() == 1);
    else if (operation == DatabaseJob::Restore) job->restore(profile, password, database(), file->text(), clean->isChecked(), skipOwners->isChecked());
    else if (operation == DatabaseJob::Create) job->create(profile, password, database());
    else job->drop(profile, password, database());
    updateControls();
}

// Use monotonic wall duration, including cancellation and the final atomic file copy.
// Freeze the final value on every completion, without estimating remaining work.
void DatabaseTaskDialog::updateDuration()
{
    const qint64 milliseconds = elapsed.isValid() ? elapsed.elapsed() : 0;
    const qint64 seconds = milliseconds / 1000;
    auto time = QString("%1:%2:%3")
        .arg(seconds / 3600, 2, 10, QChar('0'))
        .arg((seconds / 60) % 60, 2, 10, QChar('0'))
        .arg(seconds % 60, 2, 10, QChar('0'));
    const bool finished = elapsed.isValid() && !durationTimer->isActive();
    if (finished) time += QString(".%1").arg(milliseconds % 1000, 3, 10, QChar('0'));
    duration->setText((finished ? tr("Totale duur: %1") : tr("Verstreken tijd: %1")).arg(time));
}

// Append readable progress while bounding both the number of lines and a pathological single line.
void DatabaseTaskDialog::appendLog(const QString &text)
{
    log->moveCursor(QTextCursor::End);
    log->insertPlainText(text);
    if (log->document()->characterCount() > 250000) log->setPlainText(log->toPlainText().right(200000));
    log->moveCursor(QTextCursor::End);
}

// Escape/Close means cancel while running; retain the log until the tool has actually stopped.
void DatabaseTaskDialog::reject()
{
    if (job->isRunning()) job->cancel();
    else QDialog::reject();
}

// Keep the job's owning widgets alive until cancellation or final file commit completes.
void DatabaseTaskDialog::closeEvent(QCloseEvent *event)
{
    if (job->isRunning()) {
        job->cancel();
        event->ignore();
    } else event->accept();
}
