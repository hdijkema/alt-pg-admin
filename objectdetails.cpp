#include "objectdetails.h"
#include "sqlhighlighter.h"
#include "pgtools.h"
#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

// Build a read-only inspector and connect replies through the current request ID.
ObjectDetails::ObjectDetails(CatalogService *catalog, QWidget *parent) : QWidget(parent), catalog(catalog)
{
    setObjectName("objectDetails");
    auto *layout = new QVBoxLayout(this);
    title = new QLabel(this);
    title->setWordWrap(true);
    layout->addWidget(title);
    tabs = new QTabWidget(this);
    tabs->setObjectName("objectDetailTabs");
    properties = new QTableWidget(0, 2, tabs);
    properties->setObjectName("objectProperties");
    properties->setHorizontalHeaderLabels({tr("Eigenschap"), tr("Waarde")});
    properties->setEditTriggers(QAbstractItemView::NoEditTriggers);
    properties->verticalHeader()->hide();
    properties->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    properties->horizontalHeader()->setStretchLastSection(true);
    tabs->addTab(properties, tr("Eigenschappen"));
    auto *sqlPage = new QWidget(tabs);
    auto *sqlLayout = new QVBoxLayout(sqlPage);
    sql = new QPlainTextEdit(sqlPage);
    sql->setObjectName("objectDefinition");
    sql->setReadOnly(true);
    sql->setLineWrapMode(QPlainTextEdit::NoWrap);
    sql->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new SqlHighlighter(sql->document());
    sqlLayout->addWidget(sql);
    auto *tools = new QHBoxLayout;
    auto *note = new QLabel(tr("pg_dump-script voor psql: alleen structuur, zonder eigenaar en privileges. "
        "Afhankelijke objecten kunnen afzonderlijk nodig zijn. Bij een kolom wordt het bovenliggende object getoond."), sqlPage);
    note->setWordWrap(true);
    tools->addWidget(note, 1);
    copy = new QPushButton(tr("SQL kopiëren"), sqlPage);
    save = new QPushButton(tr("SQL opslaan..."), sqlPage);
    tools->addWidget(copy);
    tools->addWidget(save);
    sqlLayout->addLayout(tools);
    tabs->addTab(sqlPage, tr("Definitie-SQL"));
    layout->addWidget(tabs);
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(sql->toPlainText()); });
    connect(save, &QPushButton::clicked, this, &ObjectDetails::saveSql);
    connect(tabs, &QTabWidget::currentChanged, this, [this](int index) {
        if (index == 1 && request && !dumpRequested) startDump();
    });
    connect(catalog, &CatalogService::loaded, this, [this](quint64 id, const QVariantList &rows) {
        if (id == request) showProperties(rows);
    });
    connect(catalog, &CatalogService::failed, this, [this](quint64 id, const QString &message) {
        if (id == request) title->setText(tr("Eigenschappen ophalen mislukt: %1").arg(message));
    });
    clear();
}

// Stop child processes before QObject destroys them; shutdown never leaves pg_dump running.
ObjectDetails::~ObjectDetails()
{
    for (auto *process : findChildren<QProcess *>()) {
        if (process->state() == QProcess::NotRunning) continue;
        process->kill();
        process->waitForFinished(1000);
    }
}

// Invalidate stale metadata replies and remove the previous selection and secret.
void ObjectDetails::clear()
{
    stopDump();
    request = 0;
    profile.clear();
    password.clear();
    schema.clear();
    relation.clear();
    dumpOutput.clear();
    dumpWarnings.clear();
    dumpError.clear();
    dumpRequested = false;
    properties->setRowCount(0);
    sql->clear();
    copy->setEnabled(false);
    save->setEnabled(false);
    title->setText(tr("Selecteer een database, schema, tabel, view, sequence of kolom."));
}

// Start one metadata request; selecting another object supersedes its reply.
void ObjectDetails::load(const QVariantMap &connection, const QString &secret, CatalogService::Kind kind,
                         const QString &schemaName, const QString &relationName, const QString &column)
{
    clear();
    profile = connection;
    password = secret;
    schema = schemaName;
    relation = relationName;
    title->setText(tr("Eigenschappen ophalen: %1 / %2%3%4")
        .arg(profile.value("database").toString(), schema, relation.isEmpty() ? QString() : "." + relation,
             column.isEmpty() ? QString() : "." + column));
    request = catalog->request(profile, password, kind, schema, relation, column);
    sql->setPlainText(relation.isEmpty() ? tr("Selecteer een tabel, view of sequence voor definitie-SQL.") :
        tr("Open dit tabblad om definitie-SQL op te halen."));
    if (tabs->currentIndex() == 1) startDump();
}

// Display known fields in a stable order; missing objects are reported explicitly.
void ObjectDetails::showProperties(const QVariantList &rows)
{
    if (rows.isEmpty()) {
        title->setText(tr("Dit object bestaat niet meer of is niet zichtbaar voor deze gebruiker. Vernieuw de objectboom."));
        return;
    }
    const auto values = rows.first().toMap();
    const QList<QPair<QString, QString>> fields = {
        {"name", tr("Naam")}, {"schema", tr("Schema")}, {"relation", tr("Object")}, {"type", tr("Type")},
        {"oid", "OID"}, {"owner", tr("Eigenaar")}, {"encoding", tr("Encoding")}, {"collation", tr("Collatie")},
        {"connection_limit", tr("Verbindingslimiet (-1 = onbeperkt)")}, {"allow_connections", tr("Verbindingen toegestaan")},
        {"persistence", tr("Opslag (p = permanent, u = unlogged, t = tijdelijk)")},
        {"estimated_rows", tr("Geschat aantal rijen (-1 = onbekend)")}, {"row_security", tr("Rijbeveiliging")},
        {"partition", tr("Is partitie")}, {"not_null", "NOT NULL"}, {"default_value", "DEFAULT"},
        {"identity", tr("Identity (a = altijd, d = standaard)")}, {"generated", tr("Generated (s = stored, v = virtual)")},
        {"comment", tr("Commentaar")}
    };
    for (const auto &field : fields) {
        if (!values.contains(field.first)) continue;
        const int row = properties->rowCount();
        properties->insertRow(row);
        properties->setItem(row, 0, new QTableWidgetItem(field.second));
        properties->setItem(row, 1, new QTableWidgetItem(values.value(field.first).toString()));
    }
    title->setText(profile.value("database").toString() + " / " +
        (schema.isEmpty() ? QString() : schema + ".") + values.value("name").toString());
    if (!dump && !dumpWarnings.isEmpty() && dumpError.isEmpty())
        title->setText(title->text() + tr("\npg_dump-melding: %1").arg(QString::fromUtf8(dumpWarnings)));
}

// Terminate an obsolete dump, but let its own finished signal dispose of the process.
void ObjectDetails::stopDump()
{
    if (!dump) return;
    auto *previous = dump;
    dump = nullptr;
    previous->disconnect(this);
    connect(previous, &QProcess::finished, previous, &QObject::deleteLater);
    previous->kill();
    if (previous->state() == QProcess::NotRunning) previous->deleteLater();
}

// Read pg_dump output incrementally; abort rather than displaying an incomplete definition.
void ObjectDetails::readDump()
{
    if (!dump) return;
    dumpOutput += dump->readAllStandardOutput();
    dumpWarnings += dump->readAllStandardError();
    if (dumpOutput.size() > 8 * 1024 * 1024 || dumpWarnings.size() > 65536) {
        dumpError = tr("De definitie-uitvoer is te groot. Gebruik pg_dump afzonderlijk om het volledige script op te slaan.");
        dumpOutput.clear();
        dumpWarnings.clear();
        dump->kill();
    }
}

// Fetch actual relation DDL with PostgreSQL's own tool. No shell or password argument is used.
void ObjectDetails::startDump()
{
    dumpRequested = true;
    if (relation.isEmpty()) return;
    const auto executable = PgTools::executable("pg_dump");
    if (executable.isEmpty()) {
        sql->setPlainText(tr("pg_dump ontbreekt naast de applicatie. Bouw het project opnieuw met de PostgreSQL 18-hulpmiddelen."));
        return;
    }
    auto quoted = [](QString name) { name.replace('"', "\"\""); return '"' + name + '"'; };
    dumpOutput.clear();
    dumpWarnings.clear();
    dumpError.clear();
    auto *process = new QProcess(this);
    dump = process;
    process->setProcessEnvironment(PgTools::environment(profile, password));
    sql->setPlainText(tr("Definitie-SQL ophalen..."));
    connect(process, &QProcess::readyReadStandardOutput, this, &ObjectDetails::readDump);
    connect(process, &QProcess::readyReadStandardError, this, &ObjectDetails::readDump);
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (dump != process || error != QProcess::FailedToStart) return;
        sql->setPlainText(tr("pg_dump kon niet starten: %1").arg(process->errorString()));
        dump = nullptr;
        process->deleteLater();
    });
    connect(process, &QProcess::finished, this, [this, process](int exitCode, QProcess::ExitStatus status) {
        if (dump != process) return;
        readDump();
        dump = nullptr;
        if (!dumpError.isEmpty()) sql->setPlainText(dumpError);
        else if (exitCode != 0 || status != QProcess::NormalExit) {
            auto message = QString::fromUtf8(dumpWarnings).trimmed();
            if (message.isEmpty()) message = tr("pg_dump stopte zonder foutuitvoer (code %1). "
                "Controleer de PostgreSQL-runtimebestanden naast de applicatie, waaronder liblz4.dll en libzstd.dll.").arg(exitCode);
            sql->setPlainText(tr("Definitie-SQL ophalen mislukt:\n%1").arg(message));
        }
        else {
            sql->setPlainText(QString::fromUtf8(dumpOutput));
            copy->setEnabled(!dumpOutput.isEmpty());
            save->setEnabled(!dumpOutput.isEmpty());
            if (!dumpWarnings.isEmpty()) title->setText(tr("pg_dump-melding: %1").arg(QString::fromUtf8(dumpWarnings)));
        }
        process->deleteLater();
    });
    QTimer::singleShot(30000, process, [this, process] {
        if (dump != process || process->state() == QProcess::NotRunning) return;
        dumpError = tr("Definitie-SQL ophalen duurde langer dan 30 seconden. Vernieuw om opnieuw te proberen.");
        process->kill();
    });
    process->start(executable, {"--schema-only", "--no-owner", "--no-privileges", "--no-password",
        "--strict-names", "--lock-wait-timeout=2000", "--table=" + quoted(schema) + "." + quoted(relation)});
}

// Save the displayed script atomically; failed writes leave the previous file intact.
void ObjectDetails::saveSql()
{
    const auto filename = QFileDialog::getSaveFileName(this, tr("Definitie-SQL opslaan"), "definitie.sql", tr("SQL-bestanden (*.sql)"));
    if (filename.isEmpty()) return;
    QSaveFile file(filename);
    const auto bytes = sql->toPlainText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        QMessageBox::warning(this, tr("Opslaan mislukt"), file.errorString());
}
