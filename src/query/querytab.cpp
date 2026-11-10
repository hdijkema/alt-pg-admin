#include "querytab.h"
#include "results/resultmodel.h"
#include <QToolButton>
#include <QTimer>
#include "results/cellviewer.h"
#include "sqleditor.h"
#include "database/sqlscript.h"
#include "results/resultexporter.h"
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QMimeData>
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QKeyEvent>
#include <QInputDialog>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QMenu>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableView>
#include <QTabWidget>
#include <QVBoxLayout>

// Build the editor and bounded result/detail views; session dispatch remains in PgAdmMain.
QueryTab::QueryTab(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *context = new QHBoxLayout;
    context->addWidget(new QLabel(tr("Verbinding:"), this));
    connections = new QComboBox(this);
    connections->setMinimumContentsLength(20);
    context->addWidget(connections, 1);
    state = new QLabel(tr("Gereed"), this);
    state->setObjectName("queryState");
    fullExportTimer = new QTimer(this);
    fullExportTimer->setInterval(500);
    connect(fullExportTimer, &QTimer::timeout, this, [this] { updateFullExport(fullExportRows); });
    context->addWidget(state);
    layout->addLayout(context);
    connect(connections, &QComboBox::currentIndexChanged, this, [this] { emit connectionChanged(); });
    auto *splitter = new QSplitter(Qt::Vertical, this);
    sqlEditor = new SqlEditor(splitter);
    sqlEditor->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    sqlEditor->setPlaceholderText(tr("Schrijf SQL of open een bestand. F5 voert de selectie of het hele script uit.\n-- @resultaat: Naam van het volgende resultaat"));
    sqlEditor->setLineWrapMode(QPlainTextEdit::NoWrap);
    sqlEditor->setTabStopDistance(sqlEditor->fontMetrics().horizontalAdvance(' ') * 4);
    sqlEditor->setToolTip(tr("Ctrl+F: zoeken | Ctrl+H: vervangen | F3 / Shift+F3: volgende / vorige | Ctrl+Spatie: SQL aanvullen"));
    output = new QTabWidget(splitter);
    auto *resultPanel = new QWidget(output);
    auto *resultLayout = new QVBoxLayout(resultPanel);
    resultLayout->setContentsMargins(0, 0, 0, 0);
    auto *tools = new QHBoxLayout;
    allResults = new QCheckBox(tr("Tussenresultaten tonen"), resultPanel);
    allResults->setChecked(true);
    allResults->setToolTip(tr("Geldt voor de volgende uitvoering. Zonder vinkje blijft alleen het laatste resultaat zichtbaar."));
    copyButton = new QPushButton(tr("Kopiëren"), resultPanel);
    copyButton->setObjectName("copyResultsButton");
    copyButton->setToolTip(tr("Kopieer alle getoonde resultaten naar het klembord, als tekst en als tabel voor Excel."));
    csvButton = new QPushButton(tr("CSV opslaan..."), resultPanel);
    csvButton->setObjectName("exportCsvButton");
    xlsxButton = new QPushButton(tr("Excel opslaan..."), resultPanel);
    xlsxButton->setObjectName("exportXlsxButton");
    connect(csvButton, &QPushButton::clicked, this, [this] { exportResults(false); });
    connect(xlsxButton, &QPushButton::clicked, this, [this] { exportResults(true); });
    tools->addWidget(allResults);
    tools->addStretch();
    auto *showDetails = new QCheckBox(tr("Celdetails"), resultPanel);
    showDetails->setChecked(true);
    tools->addWidget(showDetails);
    tools->addWidget(copyButton);
    tools->addWidget(csvButton);
    tools->addWidget(xlsxButton);
    fullExportButton = new QToolButton(this);
    fullExportButton->setObjectName("fullResultExportButton");
    fullExportButton->setText(tr("Uitvoeren + exporteren"));
    fullExportButton->setToolTip(tr("Voert de actuele SQL/selectie eenmaal uit en schrijft alle resultaatrijen rechtstreeks naar een bestand. De weergave blijft begrensd."));
    fullExportButton->setPopupMode(QToolButton::InstantPopup);
    auto *fullMenu = new QMenu(fullExportButton);
    fullMenu->addAction(tr("SQL uitvoeren naar volledige CSV..."), this, [this] { emit fullExportRequested(false); });
    fullMenu->addAction(tr("SQL uitvoeren naar volledige Excel..."), this, [this] { emit fullExportRequested(true); });
    fullExportButton->setMenu(fullMenu);
    context->addWidget(fullExportButton);
    resultLayout->addLayout(tools);
    auto *resultSplit = new QSplitter(Qt::Horizontal, resultPanel);
    table = new QTableView(resultSplit);
    table->setObjectName("queryResults");
    model = new ResultModel(this);
    table->setModel(model);
    table->setWordWrap(false);
    table->setAlternatingRowColors(true);
    table->setSortingEnabled(false);
    table->horizontalHeader()->setDefaultSectionSize(200);
    table->horizontalHeader()->setMinimumSectionSize(40);
    table->horizontalHeader()->setMaximumSectionSize(4096);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setResizeContentsPrecision(100);
    table->horizontalHeader()->setToolTip(tr("Sleep de scheidingslijn om de kolombreedte te wijzigen. Dubbelklik om passend te maken."));
    table->verticalHeader()->setDefaultSectionSize(26);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->horizontalHeader()->show();
    connect(table->horizontalHeader(), &QHeaderView::sectionResized, this, [this](int column, int, int size) {
        if (column < 0 || size < 40) return;
        while (widths.size() <= column) widths.append(200);
        widths[column] = size;
        emit columnWidthsChanged(columnWidths());
    });
    table->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table->horizontalHeader(), &QWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        const int column = table->horizontalHeader()->logicalIndexAt(point);
        QMenu menu(this);
        if (column >= 0) menu.addAction(tr("Kolombreedte instellen..."), this, [this, column] {
            bool ok = false;
            const int size = QInputDialog::getInt(this, tr("Kolombreedte"), tr("Breedte in pixels:"), table->columnWidth(column), 40, 4096, 1, &ok);
            if (ok) table->setColumnWidth(column, size);
        });
        menu.addAction(tr("Kolommen passend maken"), this, &QueryTab::fitColumns);
        menu.addAction(tr("Standaardbreedte herstellen"), this, [this] {
            widths.clear(); applyColumnWidths(); emit columnWidthsChanged(columnWidths());
        });
        menu.exec(table->horizontalHeader()->mapToGlobal(point));
    });
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    details = new CellViewer(resultSplit);
    details->setMinimumWidth(240);
    resultSplit->setSizes({650, 300});
    resultLayout->addWidget(resultSplit, 1);
    output->addTab(resultPanel, tr("Resultaten"));
    messages = new QPlainTextEdit(output);
    messages->setReadOnly(true);
    messages->setMaximumBlockCount(3000);
    output->addTab(messages, tr("Meldingen"));
    planPanel = new QWidget(output);
    planPanel->setObjectName("executionPlanPanel");
    auto *planLayout = new QVBoxLayout(planPanel);
    planLayout->setContentsMargins(0, 0, 0, 0);
    planControls = new QWidget(planPanel);
    auto *planOptionsLayout = new QGridLayout(planControls);
    planOptionsLayout->setContentsMargins(0, 0, 0, 0);
    const QStringList optionNames = {"ANALYZE", "VERBOSE", "COSTS", "SETTINGS", "BUFFERS",
                                     "WAL", "TIMING", "SUMMARY", "MEMORY", "GENERIC_PLAN"};
    for (int i = 0; i < optionNames.size(); ++i) {
        auto *option = new QCheckBox(optionNames.at(i), planControls);
        option->setObjectName("explain" + optionNames.at(i));
        option->setChecked(optionNames.at(i) == "COSTS" || optionNames.at(i) == "TIMING"
                           || optionNames.at(i) == "SUMMARY");
        explainOptions.append(option);
        planOptionsLayout->addWidget(option, i / 5, i % 5);
    }
    auto *analyze = explainOptions.at(0);
    auto *wal = explainOptions.at(5);
    auto *timing = explainOptions.at(6);
    auto *generic = explainOptions.at(9);
    wal->setEnabled(false);
    timing->setEnabled(false);
    planFormat = new QComboBox(planControls);
    planFormat->setObjectName("explainFormat");
    planFormat->addItems({"TEXT", "JSON", "XML", "YAML"});
    planSerialize = new QComboBox(planControls);
    planSerialize->setObjectName("explainSerialize");
    planSerialize->addItems({"NONE", "TEXT", "BINARY"});
    planSerialize->setEnabled(false);
    planOptionsLayout->addWidget(new QLabel("FORMAT", planControls), 2, 0);
    planOptionsLayout->addWidget(planFormat, 2, 1);
    planOptionsLayout->addWidget(new QLabel("SERIALIZE", planControls), 2, 2);
    planOptionsLayout->addWidget(planSerialize, 2, 3);
    auto *analyzeNotice = new QLabel(tr("ANALYZE voert de query echt uit; wijzigingen worden daarna teruggedraaid. Sequencewaarden en externe effecten kunnen wel blijven bestaan."), planPanel);
    analyzeNotice->setWordWrap(true);
    analyzeNotice->setVisible(false);
    connect(analyze, &QCheckBox::toggled, this, [this, wal, timing, generic, analyzeNotice](bool enabled) {
        if (enabled) generic->setChecked(false);
        wal->setEnabled(enabled);
        timing->setEnabled(enabled);
        planSerialize->setEnabled(enabled);
        analyzeNotice->setVisible(enabled);
    });
    connect(generic, &QCheckBox::toggled, this, [analyze](bool enabled) {
        if (enabled) analyze->setChecked(false);
    });
    planLayout->addWidget(planControls);
    planLayout->addWidget(analyzeNotice);
    plan = new QPlainTextEdit(planPanel);
    plan->setObjectName("executionPlanText");
    plan->setReadOnly(true);
    plan->setFont(sqlEditor->font());
    plan->setLineWrapMode(QPlainTextEdit::NoWrap);
    plan->setPlaceholderText(tr("F5 voert de selectie of het script uit met EXPLAIN. Bestaande EXPLAIN-opdrachten behouden hun eigen opties."));
    planLayout->addWidget(plan, 1);
    output->addTab(planPanel, tr("Uitvoeringsplan"));
    splitter->setSizes({350, 400});
    layout->addWidget(splitter, 1);
    connect(allResults, &QCheckBox::toggled, this, &QueryTab::allResultsChanged);
    connect(copyButton, &QPushButton::clicked, this, &QueryTab::copyAll);
    updateExportActions();
    connect(showDetails, &QCheckBox::toggled, details, &QWidget::setVisible);
    connect(model, &QAbstractItemModel::modelReset, table, &QTableView::clearSpans);
    connect(model, &QAbstractItemModel::modelReset, details, &CellViewer::clearValue);
    connect(table->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex &index) {
        if (const auto *cell = model->cell(index)) details->setValue(*cell);
        else details->clearValue();
    });
    connect(table, &QTableView::doubleClicked, this, [this](const QModelIndex &index) {
        const auto *cell = model->cell(index);
        if (!cell) return;
        QDialog dialog(this);
        dialog.setWindowTitle(tr("Celwaarde — %1 bytes").arg(cell->sourceBytes));
        dialog.resize(900, 650);
        auto *layout = new QVBoxLayout(&dialog);
        auto *viewer = new CellViewer(&dialog);
        viewer->setValue(*cell);
        layout->addWidget(viewer);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &dialog);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog.exec();
    });
    auto *copySelection = new QShortcut(QKeySequence::Copy, table);
    copySelection->setContext(Qt::WidgetWithChildrenShortcut);
    connect(copySelection, &QShortcut::activated, this, [this] {
        const auto indexes = table->selectionModel()->selectedIndexes();
        if (indexes.isEmpty()) return;
        int top = indexes.first().row(), bottom = top, left = indexes.first().column(), right = left;
        for (const auto &index : indexes) { top = qMin(top, index.row()); bottom = qMax(bottom, index.row()); left = qMin(left, index.column()); right = qMax(right, index.column()); }
        QString text;
        for (int row = top; row <= bottom; ++row) {
            QStringList fields;
            for (int col = left; col <= right; ++col) {
                const auto index = model->index(row, col);
                const auto *cell = model->cell(index);
                fields.append(cell ? cell->fullText() : model->data(index, Qt::DisplayRole).toString());
            }
            text += fields.join('\t') + '\n';
        }
        QApplication::clipboard()->setText(text);
    });
}

void QueryTab::showPlan() { output->setCurrentIndex(2); }
bool QueryTab::planSelected() const { return output->currentWidget() == planPanel; }
void QueryTab::setAllResults(bool enabled) { allResults->setChecked(enabled); }
QString QueryTab::connectionId() const { return connections->currentData().toString(); }
// Preserve an explicit missing target as disconnected instead of silently choosing another database.
void QueryTab::selectConnection(const QString &id)
{
    int index = connections->findData(id);
    if (index < 0 && !id.isEmpty()) {
        connections->addItem(tr("Niet verbonden"), id);
        index = connections->count() - 1;
    }
    if (index >= 0) connections->setCurrentIndex(index);
}

// Refresh available connections while retaining the previous selection, even after disconnect.
void QueryTab::setConnections(const QVariantList &profiles)
{
    const auto selected = connectionId();
    const auto previousName = connections->currentText();
    const QSignalBlocker blocker(connections);
    connections->clear();
    for (const auto &value : profiles) {
        const auto profile = value.toMap();
        connections->addItem(profile.value("name").toString(), profile.value("id"));
    }
    const int index = connections->findData(selected);
    if (index >= 0) connections->setCurrentIndex(index);
    else if (!selected.isEmpty()) {
        const auto name = previousName.endsWith(tr(" (niet verbonden)")) ? previousName : previousName + tr(" (niet verbonden)");
        connections->insertItem(0, name, selected);
        connections->setCurrentIndex(0);
    }
}

// Use the selection when present and convert editor paragraph separators back to SQL newlines.
QString QueryTab::sqlToExecute() const
{
    const auto selection = sqlEditor->textCursor().selectedText();
    return selection.isEmpty() ? sqlEditor->toPlainText() : QString(selection).replace(QChar::ParagraphSeparator, '\n');
}

// Prefix every statement separately so a script cannot accidentally execute trailing SQL normally.
// Explicit EXPLAIN statements retain the options already written by the user.
QString QueryTab::executionSql(QString *error) const
{
    error->clear();
    const auto sql = sqlToExecute();
    if (!planSelected()) return sql;
    const auto statements = SqlScript::split(sql, error);
    if (!error->isEmpty()) return {};
    QStringList options;
    const bool analyze = explainOptions.first()->isChecked();
    for (const auto *option : explainOptions) {
        const auto name = option->text();
        if ((name == "TIMING" || name == "WAL") && !analyze) continue;
        // Newer server options are omitted when disabled, keeping default plans usable on PostgreSQL 15.
        if ((name == "MEMORY" || name == "GENERIC_PLAN") && !option->isChecked()) continue;
        options.append(name + (option->isChecked() ? " TRUE" : " FALSE"));
    }
    options.append("FORMAT " + planFormat->currentText());
    if (analyze && planSerialize->currentText() != "NONE")
        options.append("SERIALIZE " + planSerialize->currentText());
    QStringList commands;
    for (const auto &statement : statements) {
        const auto prefix = statement.keyword == "EXPLAIN" ? QString() : "EXPLAIN (" + options.join(", ") + ")\n";
        const auto annotation = !prefix.isEmpty() && !statement.name.isEmpty() ? "-- @resultaat: " + statement.name + '\n' : QString();
        commands.append(annotation + prefix + statement.sql.trimmed());
    }
    return commands.join('\n');
}

// Reset output and freeze options while retaining user-adjusted column widths.
void QueryTab::startExecution()
{
    const QSignalBlocker blockWidths(table->horizontalHeader());
    running = true;
    updateExportActions();
    showAll = allResults->isChecked();
    allResults->setEnabled(false);
    connections->setEnabled(false);
    planControls->setEnabled(false);
    model->clear();
    applyColumnWidths();
    messages->clear();
    plan->clear();
    appendMessage(tr("Opdrachten worden afzonderlijk uitgevoerd; stop bij de eerste fout. Gebruik BEGIN / COMMIT voor een gezamenlijke transactie.\nDeze tab heeft een eigen sessie en transactie."));
    state->setText(tr("Bezig..."));
    if (output->currentWidget() != planPanel) output->setCurrentIndex(0);
}

// Append the worker batch, restore widths and rebuild only affected heading/status spans.
void QueryTab::appendResult(const QueryResult &result)
{
    const QSignalBlocker blockWidths(table->horizontalHeader());
    const int width = model->columnCount();
    const int start = model->append(result, showAll);
    applyColumnWidths();
    if (model->columnCount() != width) table->clearSpans();
    // Span only section headings and status lines, not every data cell.
    for (int row = model->columnCount() != width ? 0 : start; row < model->rowCount(); ++row)
        if (model->isHeading(row) && model->columnCount() > 1) table->setSpan(row, 0, 1, model->columnCount());
    if (result.finished) {
        appendMessage(QString("%1. %2: %3").arg(result.number).arg(result.title, result.status));
        if (result.columns.size() == 1 && result.columns.first() == "QUERY PLAN") {
            plan->appendPlainText(QString("%1. %2\n%3").arg(result.number).arg(result.title, model->lastPlan()));
        }
    }
}

void QueryTab::appendMessage(const QString &message) { messages->appendPlainText(message); }
// Unlock controls and record success plus the server-reported transaction state.
void QueryTab::finishExecution(bool success, const QString &transaction)
{
    if (fullExportTimer->isActive()) appendMessage(tr("Totale exportduur: %1 ms").arg(fullExportElapsed.elapsed()));
    fullExportTimer->stop();
    running = false;
    updateExportActions();
    allResults->setEnabled(true);
    connections->setEnabled(true);
    planControls->setEnabled(true);
    state->setText((success ? tr("Gereed | ") : tr("Gestopt / fout | ")) + transaction);
    appendMessage(state->text());
}

// Offer full stored overview values as both TSV text and an escaped HTML clipboard table.
void QueryTab::copyAll()
{
    auto *mime = new QMimeData;
    mime->setText(model->text());
// Escape every stored value into an overview table suitable for clipboard pasting.
    mime->setHtml(ResultExporter::html(model->results()));
    QApplication::clipboard()->setMimeData(mime);
}

// Enable output actions only for completed stored results and no active file export.
void QueryTab::updateExportActions()
{
    fullExportButton->setEnabled(canExecute && !running && !exporting);
    const bool available = !running && !exporting && !model->results().isEmpty();
    copyButton->setEnabled(available);
    csvButton->setEnabled(available);
    xlsxButton->setEnabled(available);
}

void QueryTab::setFullExportAvailable(bool enabled) { canExecute = enabled; updateExportActions(); }

// Time the explicit run, including connection establishment and final file publication.
void QueryTab::startFullExport(const QString &destination)
{
    fullExportRows = 0;
    fullExportElapsed.start();
    fullExportTimer->start();
    appendMessage(tr("Nieuwe SQL-uitvoering met volledige export naar: %1\nAlle tussenresultaten worden geschreven. SQL-wijzigingen volgen de gewone transactieregels; een bestandsfout draait ze niet terug.").arg(destination));
    updateFullExport(0);
}

// Update a bounded progress label; no full result data is retained for this display.
void QueryTab::updateFullExport(qint64 rows)
{
    fullExportRows = rows;
    state->setText(tr("Volledige export: %1 rijen | %2 s").arg(rows).arg(fullExportElapsed.elapsed() / 1000));
}

// Confirm clipping and destination, then export an immutable snapshot on a worker thread.
void QueryTab::exportResults(bool excel)
{
    if (running || exporting || model->results().isEmpty()) return;
// Inspect both section flags and stored cells before allowing an apparently complete export.
    if (ResultExporter::isTruncated(model->results()) && QMessageBox::question(this, tr("Resultaat ingekort"),
        tr("Het resultaat is niet volledig opgeslagen. Alleen het beschikbare deel exporteren?")) != QMessageBox::Yes) return;
    const auto extension = excel ? QString(".xlsx") : QString(".csv");
    auto name = path.isEmpty() ? QString("resultaat") : QFileInfo(path).completeBaseName();
    if (!property("libraryName").toString().isEmpty()) name = "resultaat";
    auto filename = QFileDialog::getSaveFileName(this, excel ? tr("Resultaat als Excel opslaan") : tr("Resultaat als CSV opslaan"),
        name + extension, excel ? tr("Excel-bestanden (*.xlsx)") : tr("CSV-bestanden (*.csv)"));
    if (filename.isEmpty()) return;
    if (!filename.endsWith(extension, Qt::CaseInsensitive)) {
        filename += extension;
        if (QFileInfo::exists(filename) && QMessageBox::question(this, tr("Bestand overschrijven"),
            tr("'%1' bestaat al. Overschrijven?").arg(filename)) != QMessageBox::Yes) return;
    }
    exporting = true;
    updateExportActions();
    auto *watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, filename] {
        const auto error = watcher->result();
        watcher->deleteLater();
        exporting = false;
        updateExportActions();
        if (!error.isEmpty()) QMessageBox::warning(this, tr("Exporteren mislukt"), error);
        else appendMessage(tr("Resultaat opgeslagen: %1").arg(filename));
    });
    // The worker owns an implicitly shared snapshot and never accesses GUI objects.
    watcher->setFuture(QtConcurrent::run([filename, excel, results = model->results(), widths = widths] {
        // Export an immutable snapshot; the XLSX wrapper handles safe file publication.
        return excel ? ResultExporter::xlsx(filename, results, widths) : ResultExporter::csv(filename, results);
    }));
}

// Serialise stored column-position widths for the owning window settings.
QVariantList QueryTab::columnWidths() const
{
    QVariantList result;
    for (int width : widths) result.append(width);
    return result;
}

// Bound restored widths and apply them without reporting programmatic changes as user edits.
void QueryTab::setColumnWidths(const QVariantList &values)
{
    widths.clear();
    for (const auto &value : values.mid(0, 4096)) widths.append(qBound(40, value.toInt(), 4096));
    applyColumnWidths();
}

// Update header sections while blocking resize signals so model resets do not overwrite preferences.
void QueryTab::applyColumnWidths()
{
    const QSignalBlocker blocker(table->horizontalHeader());
    for (int column = 0; column < model->columnCount(); ++column)
        table->setColumnWidth(column, column < widths.size() ? widths[column] : 200);
}

// Measure a bounded content sample and cap automatically fitted widths for practical viewing.
void QueryTab::fitColumns()
{
    for (int column = 0; column < model->columnCount(); ++column) {
        table->resizeColumnToContents(column);
        table->setColumnWidth(column, qBound(40, table->columnWidth(column), 800));
    }
}
