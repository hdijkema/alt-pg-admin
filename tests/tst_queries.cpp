#include "database/pgsession.h"
#include "database/sqlscript.h"
#include "results/resultmodel.h"
#include "query/querytab.h"
#include "results/cellviewer.h"
#include "query/sqlhighlighter.h"
#include "results/resultexporter.h"
#include "results/qtxlsxwriter.h"
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QTemporaryDir>
#include <QMimeData>
#include <QFileDialog>
#include <QTextBlock>
#include <QTextLayout>
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QFile>
#include <QFontDatabase>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTableView>
#include <QTest>
#include <QTimer>

class QueryTest : public QObject
{
    Q_OBJECT
    QVariantMap profile() const
    {
        return {{"host", "127.0.0.1"}, {"port", qEnvironmentVariableIntValue("PGADM_TEST_PORT")},
            {"database", "postgres"}, {"user", "postgres"}, {"sslmode", "disable"}, {"timeout", 10}};
    }
private slots:
    void initTestCase()
    {
        // The offscreen plugin has no Windows font discovery of its own.
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/arial.ttf");
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/consola.ttf");
        QFont::insertSubstitution("monospace", "Consolas");
        QFont::insertSubstitution("Courier New", "Consolas");
        QApplication::setFont(QFont("Arial", 10));
    }
    void splitScript()
    {
        QString error;
        const auto statements = SqlScript::split(QString::fromUtf8(R"SQL(
-- @resultaat: Eerste tabel
SELECT ';' AS "punt;komma", E'quote\';einde';
/* buiten /* binnen ; */ commentaar */
DO $body$ BEGIN RAISE NOTICE 'hoi; wereld'; END $body$;
SELECT $$waarde;$$;
-- alleen commentaar; geen opdracht
)SQL"), &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(statements.size(), 3);
        QCOMPARE(statements.first().name, QString("Eerste tabel"));
        QCOMPARE(SqlScript::split("SELECT 'niet af", &error).size(), 0);
        QVERIFY(!error.isEmpty());
        QFile example(QFINDTESTDATA("fixtures/performance-example.sql"));
        QVERIFY(example.open(QIODevice::ReadOnly));
        QCOMPARE(SqlScript::split(QString::fromUtf8(example.readAll()), &error).size(), 11);
        QVERIFY(error.isEmpty());
    }

    void exportResults()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QueryResult result;
        result.number = 1;
        result.title = "Overzicht";
        result.columns = {"naam", "inhoud", "leeg", "binair", "getal"};
        ResultCell name, text, null, binary, number;
        name.text = QString::fromUtf8("caf\xc3\xa9, \"Hans\"");
        text.text = "<b>letterlijk</b>\nregel\t2";
        null.isNull = true;
        binary.isBinary = true;
        binary.binary = QByteArray::fromHex("0001ff");
        number.text = "001234567890123456789";
        result.rows = {{name, text, null, binary, number}};
        text.text = "=1+1";
        name.text.clear();
        result.rows.append({name, text, null, binary, number});
        result.finished = true;
        result.status = "2 rijen";
        const auto csvPath = directory.filePath("result.csv");
        QVERIFY(ResultExporter::csv(csvPath, {result}).isEmpty());
        QFile csv(csvPath);
        QVERIFY(csv.open(QIODevice::ReadOnly));
        const auto bytes = csv.readAll();
        QVERIFY(bytes.startsWith("\xEF\xBB\xBF\"naam\",\"inhoud\",\"leeg\",\"binair\",\"getal\"\r\n"));
        QVERIFY(bytes.contains(QString::fromUtf8("\"caf\xc3\xa9, \"\"Hans\"\"\"").toUtf8()));
        QVERIFY(bytes.contains("\"<b>letterlijk</b>\nregel\t2\""));
        QVERIFY(bytes.contains("\"NULL\",\"\\x0001ff\",\"001234567890123456789\""));
        QVERIFY(!bytes.contains("Overzicht"));
        csv.close();
        QVERIFY(QDir(directory.path()).mkdir(QString::fromUtf8("caf\xc3\xa9")));
        const auto xlsxPath = directory.filePath(QString::fromUtf8("caf\xc3\xa9/resultaat \xe6\xbc\xa2.xlsx"));
        auto error = ResultExporter::xlsx(xlsxPath, {result}, {333, 250});
        QVERIFY2(error.isEmpty(), qPrintable(error));
        {
            QProcess reader;
            reader.start(QString::fromUtf8(PGADM_TEST_PYTHON), {QFINDTESTDATA("inspect-xlsx.py"), xlsxPath});
            QVERIFY(reader.waitForFinished(10000));
            QVERIFY2(reader.exitCode() == 0, reader.readAllStandardError().constData());
            const auto workbook = QJsonDocument::fromJson(reader.readAllStandardOutput()).object();
            QCOMPARE(workbook.value("sheets").toArray(), QJsonArray{"Resultaten"});
            const auto cells = workbook.value("cells").toObject();
            QCOMPARE(cells.value("A1").toString(), QString("1. Overzicht"));
            QCOMPARE(cells.value("A2").toString(), QString("naam"));
            QCOMPARE(cells.value("A3").toString(), result.rows[0][0].text);
            QCOMPARE(cells.value("B3").toString(), result.rows[0][1].text);
            QCOMPARE(cells.value("C3").toString(), QString("NULL"));
            QCOMPARE(cells.value("D3").toString(), QString("\\x0001ff"));
            QCOMPARE(cells.value("E3").toString(), number.text);
            QCOMPARE(cells.value("B4").toString(), QString("=1+1"));
            QCOMPARE(workbook.value("types").toObject().value("B4").toString(), QString("inlineStr"));
            QVERIFY(workbook.value("formulas").toArray().isEmpty());
            QVERIFY(workbook.value("bold").toArray().contains("A1"));
            QVERIFY(workbook.value("bold").toArray().contains("A2"));
            QVERIFY(!workbook.value("bold").toArray().contains("A3"));
            QVERIFY(workbook.value("widths").toArray().first().toDouble() > 40);
        }
        QFile original(xlsxPath);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const auto originalBytes = original.readAll();
        original.close();
        result.rows[0][1].text = QString(32768, 'x');
        QVERIFY(!ResultExporter::xlsx(xlsxPath, {result}, {}).isEmpty());
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), originalBytes);
        original.close();
        {
            QtXlsxWriter cancelledWriter(xlsxPath);
            QVERIFY(cancelledWriter.writeRow({"not published"}));
            std::atomic_bool cancelled{true};
            QVERIFY(!cancelledWriter.save(&cancelled).isEmpty());
            QVERIFY(original.open(QIODevice::ReadOnly));
            QCOMPARE(original.readAll(), originalBytes);
            original.close();
        }
        result.rows[0][1].text = QString("NUL") + QChar(0) + "tekst";
        QVERIFY(!ResultExporter::xlsx(xlsxPath, {result}, {}).isEmpty());
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), originalBytes);
        original.close();
        result.rows[0][1].text = QString(32768, 'x');
        QVERIFY(ResultExporter::csv(csvPath, {result}).isEmpty());
        QVERIFY(csv.open(QIODevice::ReadOnly));
        QVERIFY(csv.readAll().contains(QByteArray(32768, 'x')));
        csv.close();
        result.rows[0][1].text = "<script> & tekst";
        result.rows[0][1].truncated = true;
        result.truncated = true;
        QueryResult second;
        second.number = 2;
        second.title = "Tweede";
        second.status = "UPDATE 4";
        QVERIFY(ResultExporter::isTruncated({result, second}));
        QVERIFY(ResultExporter::csv(csvPath, {result, second}).isEmpty());
        QVERIFY(csv.open(QIODevice::ReadOnly));
        const auto combined = csv.readAll();
        QVERIFY(combined.contains("1. Overzicht"));
        QVERIFY(combined.contains("INGEKORT"));
        QVERIFY(combined.contains("2. Tweede"));
        QVERIFY(combined.contains("UPDATE 4"));
        const auto html = ResultExporter::html({result, second});
        QVERIFY(html.contains("&lt;script&gt; &amp; tekst [INGEKORT]"));
        QVERIFY(!html.contains("<script>"));
        QVERIFY(!ResultExporter::csv(directory.filePath("missing/result.csv"), {result}).isEmpty());
        QVERIFY(!ResultExporter::xlsx(directory.filePath("missing/result.xlsx"), {second}, {}).isEmpty());
    }

    void exportButtonAndClipboard()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QueryTab tab;
        auto *copy = tab.findChild<QPushButton *>("copyResultsButton");
        auto *csv = tab.findChild<QPushButton *>("exportCsvButton");
        auto *xlsx = tab.findChild<QPushButton *>("exportXlsxButton");
        QVERIFY(copy && csv && xlsx);
        QVERIFY(!copy->isEnabled() && !csv->isEnabled() && !xlsx->isEnabled());
        tab.startExecution();
        QueryResult result;
        result.number = 1;
        result.title = "Kopie";
        result.columns = {"kolom"};
        ResultCell cell;
        cell.text = "volledige waarde";
        result.rows = {{cell}};
        result.finished = true;
        result.status = "1 rij";
        tab.appendResult(result);
        QVERIFY(!csv->isEnabled());
        tab.finishExecution(true, "Autocommit");
        copy->click();
        const auto *mime = QApplication::clipboard()->mimeData();
        QVERIFY(mime->hasText() && mime->hasHtml());
        QVERIFY(mime->text().contains("volledige waarde"));
        QVERIFY(mime->html().contains("<table>"));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        for (bool excel : {false, true}) {
            const auto path = directory.filePath(excel ? "ui.xlsx" : "ui.csv");
            bool opened = false;
            QTimer::singleShot(0, &tab, [&] {
                if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                    opened = true;
                    dialog->selectFile(path);
                    QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
                }
            });
            (excel ? xlsx : csv)->click();
            QVERIFY(opened);
            QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(path), 10000);
            QTRY_VERIFY_WITH_TIMEOUT(csv->isEnabled() && xlsx->isEnabled(), 10000);
        }
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
    }

    void sqlHighlighting()
    {
        QTextDocument document;
        SqlHighlighter highlighter(&document);
        document.setPlainText("SELECT 'from', 1.5e-3, \"select\"; -- SELECT\n/* outer\n/* nested */ SELECT\n*/ SELECT $$body\n/* string body\n$$; SELECT 'multi\n/* still string'; SELECT 42;");
        document.setModified(false);
        highlighter.rehighlight();
        auto color = [&document](int line, int column) {
            for (const auto &range : document.findBlockByNumber(line).layout()->formats())
                if (column >= range.start && column < range.start + range.length) return range.format.foreground().color();
            return QColor();
        };
        const auto keyword = color(0, 0);
        const auto string = color(0, 7);
        const auto comment = color(0, 33);
        QVERIFY(keyword != string);
        QVERIFY(keyword != comment);
        QVERIFY(color(0, 15) != keyword);
        QCOMPARE(color(2, 13), comment);
        QCOMPARE(color(3, 3), keyword);
        QCOMPARE(color(4, 0), string);
        QCOMPARE(color(5, 4), keyword);
        QCOMPARE(color(6, 0), string);
        QCOMPARE(color(6, 18), keyword);
        QVERIFY(!document.isModified());
    }

    void nativeModelAndCellDetails()
    {
        QueryTab tab;
        tab.resize(1300, 850);
        tab.setConnections({QVariantMap{{"id", "example"}, {"name", "Voorbeeld / testdatabase"}}});
        tab.editor()->setPlainText("-- @resultaat: Klantenoverzicht\nSELECT naam, document FROM klanten;\n\n-- @resultaat: Plan A\nEXPLAIN (ANALYZE, BUFFERS) SELECT * FROM klanten;");
        tab.startExecution();
        QueryResult result;
        result.number = 1;
        result.title = "Klantenoverzicht";
        result.columns = {"naam", "document"};
        ResultCell text;
        text.text = QString(100000, 'x') + "\nlaatste regel";
        text.sourceBytes = text.text.size();
        ResultCell binary;
        binary.isBinary = true;
        binary.binary = QByteArray::fromHex("0001ff");
        binary.sourceBytes = 3;
        result.rows.append({text, binary});
        result.finished = true;
        result.status = "SELECT 1 | 4 ms";
        tab.appendResult(result);
        auto *table = tab.findChild<QTableView *>("queryResults");
        QVERIFY(table);
        QCOMPARE(table->horizontalHeader()->sectionResizeMode(0), QHeaderView::Interactive);
        QSignalSpy resized(&tab, &QueryTab::columnWidthsChanged);
        table->setColumnWidth(0, 333);
        table->setColumnWidth(1, 275);
        QVERIFY(!resized.isEmpty());
        QCOMPARE(tab.columnWidths(), QVariantList({333, 275}));
        auto *model = qobject_cast<ResultModel *>(table->model());
        QVERIFY(model);
        QCOMPARE(model->rowCount(), 4);
        QVERIFY(model->data(model->index(2, 0), Qt::DisplayRole).toString().size() < 250);
        QCOMPARE(model->cell(model->index(2, 0))->fullText(), text.text);
        table->setCurrentIndex(model->index(2, 0));
        auto *contents = tab.findChild<QPlainTextEdit *>("cellContents");
        QVERIFY(contents);
        QCOMPARE(contents->lineWrapMode(), QPlainTextEdit::WidgetWidth);
        QCOMPARE(contents->wordWrapMode(), QTextOption::WrapAtWordBoundaryOrAnywhere);
        QCOMPARE(contents->toPlainText().size(), 50000);
        auto *viewer = tab.findChild<CellViewer *>();
        QVERIFY(viewer);
        QPushButton *next = nullptr;
        for (auto *button : viewer->findChildren<QPushButton *>())
            if (button->text() == "Volgende") next = button;
        QVERIFY(next);
        next->click();
        next->click();
        QVERIFY(contents->toPlainText().endsWith("laatste regel"));
        bool dialogOpened = false;
        QTimer::singleShot(0, &tab, [&] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (dialog) {
                auto *detail = dialog->findChild<QPlainTextEdit *>("cellContents");
                dialogOpened = detail && detail->toPlainText().contains("00 01 ff");
                dialog->reject();
            }
        });
        QMetaObject::invokeMethod(table, "doubleClicked", Qt::DirectConnection, Q_ARG(QModelIndex, model->index(2, 1)));
        QVERIFY(dialogOpened);
        result.number = 2;
        result.title = "Plan A";
        result.columns = {"QUERY PLAN"};
        ResultCell plan;
        plan.text = "  -> Seq Scan on klanten";
        result.rows = {{plan}};
        tab.appendResult(result);
        tab.copyAll();
        const auto copied = QApplication::clipboard()->text();
        QVERIFY(copied.contains("Klantenoverzicht"));
        QVERIFY(copied.contains("laatste regel"));
        QVERIFY(copied.contains("\\x0001ff"));
        QVERIFY(copied.contains("  -> Seq Scan"));
        tab.finishExecution(true, "Autocommit");
        tab.show();
        QTest::qWait(50);
        QPixmap screenshot(tab.size());
        tab.render(&screenshot);
        QVERIFY(screenshot.save("query-results-preview.png"));
        ResultModel lastOnly;
        lastOnly.append(result, false);
        result.number = 3;
        result.title = "Laatste";
        lastOnly.append(result, false);
        QVERIFY(!lastOnly.text().contains("Plan A"));
        QVERIFY(lastOnly.text().contains("Laatste"));
        tab.startExecution();
        tab.appendResult(result);
        QCOMPARE(table->columnWidth(0), 333);
        result.columns = {"naam", "document"};
        result.rows = {{text, binary}};
        ++result.number;
        tab.appendResult(result);
        QCOMPARE(table->columnWidth(1), 275);
        QueryTab restored;
        restored.setColumnWidths(tab.columnWidths());
        restored.appendResult(result);
        QCOMPARE(restored.findChild<QTableView *>("queryResults")->columnWidth(0), 333);
    }

    void multipleQueriesAndPlans()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        PgSession session;
        QSignalSpy connected(&session, &PgSession::connected);
        session.open(profile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        ResultModel model;
        connect(&session, &PgSession::resultReady, &model, [&](const QueryResult &result) { model.append(result, true); });
        QSignalSpy finished(&session, &PgSession::executionFinished);
        QSignalSpy notices(&session, &PgSession::notice);
        QVERIFY(session.execute(QString::fromUtf8(R"SQL(
CREATE TEMP TABLE proef (id int, naam text);
INSERT INTO proef VALUES (1, 'a;b'), (2, 'tweede');
-- @resultaat: Klantenoverzicht
SELECT * FROM proef ORDER BY id;
UPDATE proef SET naam = 'gewijzigd' WHERE id = 1;
-- @resultaat: Speciale waarden
SELECT NULL::text AS leeg, ''::text AS tekst, decode('0001ff', 'hex') AS bytes, repeat('lang ', 10000) AS groot;
-- @resultaat: Plan A
EXPLAIN (ANALYZE, BUFFERS) SELECT * FROM proef;
-- @resultaat: Plan B
EXPLAIN (ANALYZE, BUFFERS) SELECT count(*) FROM proef;
DO $body$ BEGIN RAISE NOTICE 'bericht; met puntkomma'; END $body$;
)SQL")));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(finished.first().at(0).toBool());
        const auto text = model.text();
        QVERIFY(text.contains("Klantenoverzicht"));
        QVERIFY(text.contains("a;b"));
        QVERIFY(text.contains("UPDATE 1"));
        QVERIFY(text.contains("\\x0001ff"));
        QVERIFY(text.contains("Plan A"));
        QVERIFY(text.contains("Plan B"));
        QCOMPARE(text.count("QUERY PLAN"), 2);
        QVERIFY(!model.lastPlan().isEmpty());
        QVERIFY(notices.count() > 0);
        QVERIFY(!session.isBusy());
    }

    void fullResultExport()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        QTemporaryDir directory;
        PgSession session;
        QSignalSpy connected(&session, &PgSession::connected);
        session.open(profile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QSignalSpy finished(&session, &PgSession::executionFinished);
        QSignalSpy exported(&session, &PgSession::exportFinished);
        ResultModel model;
        connect(&session, &PgSession::resultReady, &model, [&model](const QueryResult &result) { model.append(result, true); });
        QVERIFY(session.execute("CREATE TEMP TABLE export_effects (id integer);"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        finished.clear();
        model.clear();
        const auto csvPath = directory.filePath("full.csv");
        QVERIFY(session.executeToFile("INSERT INTO export_effects VALUES (1);\n-- @resultaat: Alles\n"
            "SELECT i, repeat('x',700) AS tekst FROM generate_series(1,60001) i;\n"
            "-- @resultaat: Leeg\nSELECT 1 AS kolom WHERE false;\n"
            "-- @resultaat: Een uitvoering\nSELECT count(*) FROM export_effects;", csvPath, false));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
        QVERIFY2(finished.first().first().toBool(), qPrintable(model.text().right(1000)));
        QCOMPARE(exported.count(), 1);
        QVERIFY(exported.first().first().toBool());
        QCOMPARE(exported.first().at(2).toLongLong(), 60002LL);
        QVERIFY(ResultExporter::isTruncated(model.results()));
        QVERIFY(model.rowCount() < 20020);
        QFile csv(csvPath);
        QVERIFY(csv.open(QIODevice::ReadOnly));
        QVERIFY(csv.size() > 40 * 1024 * 1024);
        bool lastRow = false, emptySection = false, once = false;
        qint64 dataRows = 0;
        while (!csv.atEnd()) {
            const auto line = csv.readLine();
            if (line.endsWith(QByteArray(700, 'x') + "\"\r\n")) ++dataRows;
            if (line.startsWith("\"60001\",")) lastRow = true;
            if (line.contains("3. Leeg")) emptySection = true;
            if (line == "\"1\"\r\n") once = true;
            QVERIFY(!line.contains("INGEKORT"));
        }
        QCOMPARE(dataRows, 60001LL);
        QVERIFY(lastRow && emptySection && once);
        csv.close();

        finished.clear();
        exported.clear();
        model.clear();
        const auto largePath = directory.filePath("large-cell.csv");
        QVERIFY(session.executeToFile("SELECT repeat('z',3*1024*1024) AS groot;", largePath, false));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(exported.first().first().toBool());
        QFile large(largePath);
        QVERIFY(large.open(QIODevice::ReadOnly));
        const auto largeBytes = large.readAll();
        QVERIFY(largeBytes.contains(QByteArray(3*1024*1024, 'z')));
        QVERIFY(!largeBytes.contains("INGEKORT"));
        QVERIFY(ResultExporter::isTruncated(model.results()));

        finished.clear();
        exported.clear();
        const auto valuesPath = directory.filePath("values.csv");
        QVERIFY(session.executeToFile(QString::fromUtf8("SET bytea_output='escape'; SELECT 'caf\xc3\xa9,\"Hans\"' AS naam, E'<b>letterlijk</b>\\nregel' AS tekst, NULL::text AS leeg, decode('0001ff','hex') AS binair; RESET bytea_output;"), valuesPath, false));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(exported.first().first().toBool());
        QFile values(valuesPath);
        QVERIFY(values.open(QIODevice::ReadOnly));
        const auto valueBytes = values.readAll();
        QVERIFY(valueBytes.contains(QString::fromUtf8("\"caf\xc3\xa9,\"\"Hans\"\"\"").toUtf8()));
        QVERIFY(valueBytes.contains("\"<b>letterlijk</b>\nregel\""));
        QVERIFY(valueBytes.contains("\"NULL\",\"\\x0001ff\""));

        finished.clear();
        exported.clear();
        model.clear();
        const auto xlsxPath = directory.filePath("full.xlsx");
        QVERIFY(session.executeToFile("SELECT i::text AS id, '=1+1' AS literal, '001234567890123456789' AS exact FROM generate_series(1,25001) i;", xlsxPath, true, {333}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
        QVERIFY(exported.first().first().toBool());
        QCOMPARE(exported.first().at(2).toLongLong(), 25001LL);
        QVERIFY(ResultExporter::isTruncated(model.results()));
        QProcess reader;
        reader.start(QString::fromUtf8(PGADM_TEST_PYTHON), {QFINDTESTDATA("inspect-xlsx.py"), xlsxPath});
        QVERIFY(reader.waitForFinished(10000));
        QVERIFY2(reader.exitCode() == 0, reader.readAllStandardError().constData());
        const auto workbook = QJsonDocument::fromJson(reader.readAllStandardOutput()).object();
        const auto cells = workbook.value("cells").toObject();
        QCOMPARE(cells.value("A25003").toString(), QString("25001"));
        QCOMPARE(cells.value("B25003").toString(), QString("=1+1"));
        QCOMPARE(cells.value("C25003").toString(), QString("001234567890123456789"));
        QVERIFY(workbook.value("formulas").toArray().isEmpty());
        QVERIFY(workbook.value("widths").toArray().first().toDouble() > 40);
    }

    void fullExportFailureAndCancel()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        QTemporaryDir directory;
        const auto path = directory.filePath("preserved.csv");
        QFile previous(path);
        QVERIFY(previous.open(QIODevice::WriteOnly));
        previous.write("previous file");
        previous.close();
        PgSession session;
        QSignalSpy connected(&session, &PgSession::connected);
        session.open(profile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QSignalSpy finished(&session, &PgSession::executionFinished);
        QSignalSpy exported(&session, &PgSession::exportFinished);
        QVERIFY(session.execute("CREATE TEMP TABLE export_once (id int); INSERT INTO export_once VALUES(1);"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        for (int scenario = 0; scenario < 4; ++scenario) {
            finished.clear();
            exported.clear();
            const auto destination = scenario == 3 ? directory.filePath("missing/result.csv") : path;
            const QStringList scripts{
                "SELECT generate_series(1,1000); SELECT 1/0; INSERT INTO export_once VALUES(2);",
                "SELECT repeat('x',32768); INSERT INTO export_once VALUES(2);",
                "SELECT i, pg_sleep(0.001) FROM generate_series(1,20000) i; INSERT INTO export_once VALUES(2);",
                "INSERT INTO export_once VALUES(2); SELECT 42;"};
            QVERIFY(session.executeToFile(scripts[scenario], destination, scenario == 1));
            if (scenario == 2) QTimer::singleShot(120, &session, &PgSession::cancel);
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
            QCOMPARE(exported.count(), 1);
            QVERIFY(!exported.first().first().toBool());
            QVERIFY(previous.open(QIODevice::ReadOnly));
            QCOMPARE(previous.readAll(), QByteArray("previous file"));
            previous.close();
            finished.clear();
            QSignalSpy results(&session, &PgSession::resultReady);
            QVERIFY(session.execute("SELECT count(*) FROM export_once;"));
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
            QVERIFY(finished.first().first().toBool());
            QString value;
            for (const auto &signal : results)
                for (const auto &row : qvariant_cast<QueryResult>(signal.first()).rows) value = row.first().text;
            QCOMPARE(value, QString("1")); // No replay, no later statement after failure, reusable session.
        }
        QVERIFY(!QFileInfo::exists(directory.filePath("missing/result.csv")));
    }

    void errorAndRollback()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        PgSession session;
        QSignalSpy connected(&session, &PgSession::connected);
        session.open(profile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QSignalSpy results(&session, &PgSession::resultReady);
        QSignalSpy finished(&session, &PgSession::executionFinished);
        QVERIFY(session.execute("BEGIN; SELECT 1 / 0; SELECT 'niet uitvoeren';"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(finished.first().at(1).toString().contains("ROLLBACK"));
        int completed = 0;
        for (const auto &signal : results) {
            const auto result = qvariant_cast<QueryResult>(signal.first());
            QVERIFY(result.number <= 2);
            if (result.finished) ++completed;
        }
        QCOMPARE(completed, 2);
        QVERIFY(session.execute("ROLLBACK; SELECT 42;"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
        QVERIFY(finished.last().at(0).toBool());
    }

    void cancelAndReuseConnection()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        PgSession session;
        QSignalSpy connected(&session, &PgSession::connected);
        session.open(profile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QSignalSpy finished(&session, &PgSession::executionFinished);
        QVERIFY(session.execute("SELECT pg_sleep(20); SELECT 999;"));
        QTimer::singleShot(150, &session, &PgSession::cancel);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(!finished.first().at(0).toBool());
        QCOMPARE(session.state(), PgSession::Connected);
        QVERIFY(session.execute("SELECT 42;"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 5000);
        QVERIFY(finished.last().at(0).toBool());
    }

    void limitsAndLargeCells()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        PgSession session;
        QSignalSpy connected(&session, &PgSession::connected);
        session.open(profile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QSignalSpy finished(&session, &PgSession::executionFinished);
        int rows = 0;
        bool truncated = false, largeCell = false;
        connect(&session, &PgSession::resultReady, this, [&](const QueryResult &result) {
            rows += result.number == 1 ? result.rows.size() : 0;
            truncated |= result.truncated;
            for (const auto &row : result.rows)
                if (result.number == 2) { largeCell = row.first().truncated; QVERIFY(row.first().text.size() <= 2 * 1024 * 1024); }
        });
        QVERIFY(session.execute("SELECT generate_series(1, 20005); SELECT repeat('x', 3000000);"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
        QVERIFY(finished.first().at(0).toBool());
        QCOMPARE(rows, 20000);
        QVERIFY(truncated);
        QVERIFY(largeCell);
    }
};
QTEST_MAIN(QueryTest)
#include "tst_queries.moc"
