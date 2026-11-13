#include "connections/passwordstore.h"
#include "database/catalogservice.h"
#include "browser/catalogtree.h"
#include "app/pgadmmain.h"
#include "database/pgsession.h"
#include "query/querytab.h"
#include "results/resultmodel.h"
#include "query/savedqueriespanel.h"
#include "browser/objectdetails.h"
#include "connections/profiledialog.h"
#include "administration/databasejob.h"
#include "administration/databasetaskdialog.h"
#include "administration/rolemanager.h"
#include "administration/rolesql.h"
#include "query/sqleditor.h"
#include <QCompleter>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QFileDialog>
#include <QDockWidget>
#include <QDialog>
#include <QDialogButtonBox>
#include <QProcess>
#include <QTableWidget>
#include <QInputDialog>
#include <QListWidget>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTextDocument>
#include <QHeaderView>
#include <QAction>
#include <QApplication>
#include <QFontDatabase>
#include <QFile>
#include <QMenu>
#include <QPlainTextEdit>
#include <QSettings>
#include <QSignalSpy>
#include <QTableView>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QPointer>
#include <QToolButton>
#include <QTest>
#include <QUuid>
#include <QTranslator>
#include <QScopeGuard>

class BrowserTest : public QObject
{
    Q_OBJECT
    const QString suffix = QUuid::createUuid().toString(QUuid::Id128).left(12);
    const QString schema = "browser' " + suffix;
    const QString database = "browser_" + suffix;
    const QString service = "PgAdmQt.Tests." + suffix;
    const QString secretKey = "credential/" + suffix;
    bool keychainRoundTripStarted = false;
    QString mainCredentialKey;
    QVariantMap profile() const
    {
        return {{"id", "test-" + suffix}, {"name", "Testserver"}, {"host", "127.0.0.1"},
            {"port", qEnvironmentVariableIntValue("PGADM_TEST_PORT")}, {"database", "postgres"},
            {"user", "postgres"}, {"sslmode", "disable"}, {"timeout", 10}};
    }
    static QTreeWidgetItem *child(QTreeWidgetItem *parent, const QString &name)
    {
        if (!parent) return nullptr;
        for (int i = 0; i < parent->childCount(); ++i)
            if (parent->child(i)->text(0) == name) return parent->child(i);
        return nullptr;
    }
    static bool triggerContextAction(PgAdmMain &window, CatalogTree *tree, QTreeWidgetItem *database, const QString &actionName = "openDatabaseQueryAction")
    {
        tree->setCurrentItem(database);
        bool triggered = false;
        QTimer::singleShot(0, &window, [&] {
            auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu) return;
            for (auto *action : menu->actions()) {
                if (action->objectName() != actionName || !action->isEnabled()) continue;
                action->trigger();
                triggered = true;
                break;
            }
            menu->close();
        });
        tree->customContextMenuRequested(tree->visualItemRect(database).center());
        return triggered;
    }
private slots:
    void initTestCase()
    {
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/arial.ttf");
        QFontDatabase::addApplicationFont("C:/Windows/Fonts/consola.ttf");
        QFont::insertSubstitution("monospace", "Consolas");
        QApplication::setFont(QFont("Arial", 10));
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) return;
        PgSession session;
        QSignalSpy connected(&session, &PgSession::connected);
        session.open(profile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QSignalSpy finished(&session, &PgSession::executionFinished);
        QVERIFY(session.execute("CREATE DATABASE \"" + database + "\"; CREATE SCHEMA \"" + schema +
            "\"; CREATE TABLE \"" + schema + "\".voorbeeld (id integer NOT NULL, inhoud text); CREATE VIEW \"" +
            schema + "\".voorbeeld_view AS SELECT * FROM \"" + schema + "\".voorbeeld; GRANT USAGE ON SCHEMA \"" + schema + "\" TO pgadm_test_auth; CREATE DATABASE lts_ontwikkel;"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(finished.first().at(0).toBool());
    }

    void translations_data()
    {
        QTest::addColumn<QString>("language");
        QTest::addColumn<QString>("connectText");
        QTest::addColumn<QString>("restartText");
        QTest::newRow("Dutch") << "nl" << "Verbinden"
            << "De taal wordt toegepast wanneer je de applicatie opnieuw start.";
        QTest::newRow("English") << "en" << "Connect"
            << "The language will be applied when you restart the application.";
        QTest::newRow("German") << "de" << "Verbinden"
            << "Die Sprache wird beim nächsten Start der Anwendung übernommen.";
    }

    void translations()
    {
        QFETCH(QString, language);
        QFETCH(QString, connectText);
        QFETCH(QString, restartText);
        QTranslator translator;
        if (language != "nl") {
            QVERIFY(translator.load(":/i18n/pgadmin_" + language + ".qm"));
            QVERIFY(qApp->installTranslator(&translator));
        }
        const auto removeTranslator = qScopeGuard([&] { qApp->removeTranslator(&translator); });
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto settingsPath = temporary.filePath("settings.ini");
        PgAdmMain window(nullptr, settingsPath);
        auto *connectAction = window.findChild<QAction *>("connectProfileAction");
        QVERIFY(connectAction);
        QCOMPARE(connectAction->text(), connectText);
        auto *english = window.findChild<QAction *>("language_en");
        QVERIFY(english);
        QString message;
        QTimer::singleShot(0, &window, [&] {
            auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (dialog) { message = dialog->text(); dialog->accept(); }
        });
        english->trigger();
        QCOMPARE(message, restartText);
        QCOMPARE(QSettings(settingsPath, QSettings::IniFormat).value("ui/language").toString(), QString("en"));
        QCOMPARE(connectAction->text(), connectText);
        PgAdmMain restored(nullptr, settingsPath);
        QVERIFY(restored.findChild<QAction *>("language_en")->isChecked());
    }

    void savedQueriesAndWidths()
    {
        QTemporaryDir temporary;
        QVERIFY(temporary.isValid());
        const auto settingsPath = temporary.filePath("settings.ini");
        const QString sql = QString::fromUtf8("SELECT 'caf\xc3\xa9';\nSELECT 42;");
        {
            PgAdmMain window(nullptr, settingsPath);
            window.show();
            auto *tabs = window.findChild<QTabWidget *>("queryTabs");
            auto *panel = window.findChild<SavedQueriesPanel *>();
            auto *list = window.findChild<QListWidget *>("savedQueryList");
            QVERIFY(tabs && panel && list);
            auto *tab = qobject_cast<QueryTab *>(tabs->currentWidget());
            tab->editor()->setPlainText(sql);
            auto enterName = [&window](const QString &name) {
                QTimer::singleShot(0, &window, [name] {
                    if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                        dialog->setTextValue(name);
                        dialog->accept();
                    }
                });
            };
            enterName("Mijn overzicht");
            window.findChild<QAction *>("saveNamedQueryAction")->trigger();
            QCOMPARE(list->count(), 1);
            QCOMPARE(tabs->tabText(tabs->currentIndex()), QString("Mijn overzicht"));
            QString error;
            QCOMPARE(panel->library().load("Mijn overzicht", &error), sql);
            QVERIFY(error.isEmpty());
            QCOMPARE(panel->library().category("Mijn overzicht"), QString());
            auto *categories = panel->findChild<QComboBox *>("savedQueryCategory");
            QVERIFY(categories);
            enterName("LTS / beheer");
            window.findChild<QPushButton *>("categoriseSavedQueryButton")->click();
            QCOMPARE(panel->library().category("Mijn overzicht"), QString("LTS / beheer"));
            QCOMPARE(panel->library().load("Mijn overzicht", &error), sql);
            const auto unchangedPath = tab->filePath();
            QCOMPARE(unchangedPath, panel->library().filePath("Mijn overzicht"));
            QVERIFY(panel->library().setCategory("Mijn overzicht", "lts / BEHEER", &error));
            QCOMPARE(panel->library().categories(), QStringList{"LTS / beheer"});
            QVERIFY(!panel->library().setCategory("Mijn overzicht", "bad\ncategory", &error));
            QVERIFY(!panel->library().setCategory("missing query", "LTS", &error));
            categories->setCurrentIndex(categories->findData(QString("")));
            QVERIFY(list->item(0)->isHidden());
            QVERIFY(!window.findChild<QPushButton *>("categoriseSavedQueryButton")->isEnabled());
            categories->setCurrentIndex(categories->findData("LTS / beheer"));
            QVERIFY(!list->item(0)->isHidden());
            QVERIFY(!panel->library().save("mijn overzicht", "overwrite", false, &error));
            QCOMPARE(panel->library().load("Mijn overzicht", &error), sql);
            tabs->tabCloseRequested(tabs->currentIndex());
            panel->openRequested("Mijn overzicht");
            tab = qobject_cast<QueryTab *>(tabs->currentWidget());
            QCOMPARE(tab->editor()->toPlainText(), sql);
            tab->editor()->appendPlainText("-- bewerkt");
            const auto edited = tab->editor()->toPlainText();
            const int count = tabs->count();
            panel->openRequested("Mijn overzicht");
            QCOMPARE(tabs->count(), count);
            QCOMPARE(tab->editor()->toPlainText(), edited);
            window.findChild<QAction *>("saveQueryAction")->trigger();
            QCOMPARE(panel->library().load("Mijn overzicht", &error), edited);
            QueryResult result;
            result.columns = {"waarde"};
            tab->appendResult(result);
            tab->findChild<QTableView *>("queryResults")->setColumnWidth(0, 347);
            enterName("Nieuw overzicht");
            window.findChild<QPushButton *>("renameSavedQueryButton")->click();
            QCOMPARE(tab->property("libraryName").toString(), QString("Nieuw overzicht"));
            QCOMPARE(tab->filePath(), panel->library().filePath("Nieuw overzicht"));
            QVERIFY(panel->library().existingName("Mijn overzicht").isEmpty());
            QCOMPARE(panel->library().load("Nieuw overzicht", &error), edited);
            QCOMPARE(panel->library().category("Nieuw overzicht"), QString("LTS / beheer"));
            window.findChild<QLineEdit *>("savedQuerySearch")->setText("onvindbaar");
            QVERIFY(list->item(0)->isHidden());
            window.findChild<QLineEdit *>("savedQuerySearch")->clear();
            window.resize(1400, 850);
            QTest::qWait(50);
            QPixmap preview(window.size());
            window.render(&preview);
            QVERIFY(preview.save("saved-queries-preview.png"));
            QVERIFY(window.close());
        }
        {
            PgAdmMain reopened(nullptr, settingsPath);
            auto *panel = reopened.findChild<SavedQueriesPanel *>();
            QCOMPARE(panel->library().names(), QStringList({"Nieuw overzicht"}));
            QCOMPARE(panel->library().category("Nieuw overzicht"), QString("LTS / beheer"));
            auto *tabs = reopened.findChild<QTabWidget *>("queryTabs");
            auto *tab = qobject_cast<QueryTab *>(tabs->currentWidget());
            QCOMPARE(tab->columnWidths(), QVariantList{QVariant(347)});
            panel->openRequested("Nieuw overzicht");
            tab = qobject_cast<QueryTab *>(tabs->currentWidget());
            QCOMPARE(tab->columnWidths(), QVariantList{QVariant(347)});
            QString error;
            QVERIFY(panel->library().save("Andere query", "SELECT 2;", false, &error));
            auto *categories = panel->findChild<QComboBox *>("savedQueryCategory");
            categories->setCurrentIndex(categories->findData("LTS / beheer"));
            panel->refresh();
            auto *list = panel->findChild<QListWidget *>("savedQueryList");
            QVERIFY(list->findItems("Andere query", Qt::MatchExactly).first()->isHidden());
            QVERIFY(panel->library().setCategory("Andere query", "Onderhoud", &error));
            panel->refresh("Andere query");
            QCOMPARE(categories->currentData().toString(), QString("Onderhoud"));
            QVERIFY(!list->findItems("Andere query", Qt::MatchExactly).first()->isHidden());
            panel->openRequested("Andere query");
            auto *other = qobject_cast<QueryTab *>(tabs->currentWidget());
            QueryResult result;
            result.columns = {"twee"};
            other->appendResult(result);
            other->findChild<QTableView *>("queryResults")->setColumnWidth(0, 222);
            tabs->tabCloseRequested(tabs->currentIndex());
            panel->openRequested("Andere query");
            QCOMPARE(qobject_cast<QueryTab *>(tabs->currentWidget())->columnWidths(), QVariantList{QVariant(222)});
            panel->openRequested("Nieuw overzicht");
            QCOMPARE(tab->columnWidths(), QVariantList{QVariant(347)});
            QVERIFY(tab->editor()->toPlainText().endsWith("-- bewerkt"));
            QTimer::singleShot(0, &reopened, [] {
                if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                    dialog->button(QMessageBox::Yes)->click();
            });
            panel->refresh("Nieuw overzicht");
            reopened.findChild<QPushButton *>("removeSavedQueryButton")->click();
            QCOMPARE(panel->library().names(), QStringList{"Andere query"});
            QCOMPARE(panel->library().categories(), QStringList{"Onderhoud"});
            QVERIFY(panel->library().setCategory("Andere query", "", &error));
            panel->refresh("Andere query");
            QVERIFY(panel->library().categories().isEmpty());
            QVERIFY(tab->filePath().isEmpty());
            QVERIFY(tab->editor()->document()->isModified());
            QVERIFY(tab->editor()->toPlainText().endsWith("-- bewerkt"));
        }
    }

    // Cancellation must preserve the caller's profile and secret; accepted secrets
    // stay separate from persisted profile fields after the dialog extraction.
    void editorSearchAndCompletion()
    {
        SqlEditor editor;
        editor.resize(900, 500);
        editor.show();
        const QString original = "SELECT name, surname FROM people;\nselect name;";
        editor.setPlainText(original);
        editor.showSearch(true);
        auto *needle = editor.findChild<QLineEdit *>("findText");
        auto *replacement = editor.findChild<QLineEdit *>("replaceText");
        needle->setText("name");
        replacement->setText("name_name");
        editor.findChildren<QCheckBox *>().last()->setChecked(true);
        editor.findChild<QPushButton *>("replaceAll")->click();
        QCOMPARE(editor.toPlainText(), QString("SELECT name_name, surname FROM people;\nselect name_name;"));
        editor.undo();
        QCOMPARE(editor.toPlainText(), original);
        needle->setText("SELECT");
        replacement->setText("FETCH");
        editor.findChildren<QCheckBox *>().first()->setChecked(true);
        editor.findChild<QPushButton *>("replaceAll")->click();
        QCOMPARE(editor.toPlainText(), QString("FETCH name, surname FROM people;\nselect name;"));
        editor.undo();
        needle->setText("name");
        replacement->setText("label");
        editor.moveCursor(QTextCursor::Start);
        editor.findNext();
        editor.findChild<QPushButton *>("replaceOne")->click();
        QCOMPARE(editor.toPlainText(), QString("SELECT label, surname FROM people;\nselect name;"));
        editor.undo();
        replacement->clear();
        editor.findChild<QPushButton *>("replaceAll")->click();
        QCOMPARE(editor.toPlainText(), QString("SELECT , surname FROM people;\nselect ;"));
        editor.undo();
        editor.moveCursor(QTextCursor::End);
        editor.findNext();
        QCOMPARE(editor.textCursor().selectionStart(), 7);
        editor.findNext(true);
        QVERIFY(editor.textCursor().selectionStart() > 30);
        needle->setText("missing");
        const int position = editor.textCursor().position();
        editor.findNext();
        QCOMPARE(editor.textCursor().position(), position);
        editor.findChild<QDialog *>()->hide();
        editor.activateWindow();
        editor.setFocus();
        QTRY_VERIFY(editor.isActiveWindow());
        QTRY_VERIFY(editor.hasFocus());
        QTest::keyClick(&editor, Qt::Key_F, Qt::ControlModifier);
        QTRY_VERIFY(editor.findChild<QDialog *>()->isVisible());
        editor.findChild<QDialog *>()->hide();
        editor.setPlainText("SEL");
        editor.moveCursor(QTextCursor::End);
        editor.activateWindow();
        editor.setFocus();
        QTest::keyClick(&editor, Qt::Key_Space, Qt::ControlModifier);
        auto *completion = editor.findChild<QCompleter *>();
        QTRY_VERIFY(completion->popup()->isVisible());
        QCOMPARE(completion->currentCompletion(), QString("SELECT"));
        QVERIFY(completion->popup()->currentIndex().isValid());
        QTest::keyClick(completion->popup(), Qt::Key_Return);
        QCOMPARE(editor.toPlainText(), QString("SELECT"));
        editor.undo();
        QCOMPARE(editor.toPlainText(), QString("SEL"));
    }

    void aboutApplicationIcon()
    {
        QTemporaryDir temporary;
        PgAdmMain window(nullptr, temporary.filePath("settings.ini"));
        bool rendered = false;
        QTimer::singleShot(0, &window, [&] {
            if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) {
                rendered = dialog->objectName() == "aboutDialog" && !dialog->iconPixmap().isNull()
                    && dialog->windowTitle() == "pgadmin-jr" && dialog->text().contains("libpq:");
                dialog->accept();
            }
        });
        window.findChild<QAction *>("aboutAction")->trigger();
        QVERIFY(rendered);
    }

    void editorTabsRestore()
    {
        QTemporaryDir temporary;
        const auto settingsPath = temporary.filePath("settings.ini");
        const auto sqlPath = temporary.filePath("external.sql");
        const auto crlfPath = temporary.filePath("windows.sql");
        { QFile file(crlfPath); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("SELECT 4;\r\nSELECT 5;"); }
        { QFile file(sqlPath); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("SELECT 2;"); }
        {
            PgAdmMain window(nullptr, settingsPath);
            auto *tabs = window.findChild<QTabWidget *>("queryTabs");
            auto *draft = qobject_cast<QueryTab *>(tabs->currentWidget());
            draft->editor()->setPlainText("SELECT 'unsaved';");
            draft->editor()->document()->setModified(true);
            draft->selectConnection("unconnected/profile");
            QTimer::singleShot(0, &window, [] {
                if (auto *dialog = qobject_cast<QInputDialog *>(QApplication::activeModalWidget())) {
                    dialog->setTextValue("Mijn analyse");
                    dialog->accept();
                }
            });
            tabs->tabBarDoubleClicked(0);
            QCOMPARE(tabs->tabText(0), QString("Mijn analyse *"));
            QVERIFY(draft->filePath().isEmpty());
            draft->setColumnWidths({333, 222});
            auto cursor = draft->editor()->textCursor();
            cursor.setPosition(9);
            draft->editor()->setTextCursor(cursor);
            window.findChild<QAction *>("newQueryAction")->trigger();
            auto *file = qobject_cast<QueryTab *>(tabs->currentWidget());
            file->setFilePath(sqlPath);
            file->editor()->setPlainText("SELECT 2;");
            file->editor()->document()->setModified(false);
            window.findChild<QAction *>("newQueryAction")->trigger();
            auto *crlf = qobject_cast<QueryTab *>(tabs->currentWidget());
            crlf->setFilePath(crlfPath);
            crlf->editor()->setPlainText("SELECT 4;\nSELECT 5;");
            crlf->editor()->document()->setModified(false);
            tabs->setCurrentIndex(0);
            QVERIFY(window.close());
        }
        { QFile file(sqlPath); QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate)); file.write("SELECT 3;"); }
        PgAdmMain reopened(nullptr, settingsPath);
        auto *tabs = reopened.findChild<QTabWidget *>("queryTabs");
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(tabs->currentIndex(), 0);
        auto *draft = qobject_cast<QueryTab *>(tabs->widget(0));
        QCOMPARE(draft->editor()->toPlainText(), QString("SELECT 'unsaved';"));
        QVERIFY(draft->editor()->document()->isModified());
        QCOMPARE(draft->editor()->textCursor().position(), 9);
        QCOMPARE(draft->connectionId(), QString("unconnected/profile"));
        QCOMPARE(tabs->tabText(0), QString("Mijn analyse *"));
        QCOMPARE(draft->columnWidths(), QVariantList({333, 222}));
        auto *file = qobject_cast<QueryTab *>(tabs->widget(1));
        QCOMPARE(file->editor()->toPlainText(), QString("SELECT 2;"));
        QVERIFY(file->editor()->document()->isModified());
        QVERIFY(!qobject_cast<QueryTab *>(tabs->widget(2))->editor()->document()->isModified());
        QVERIFY(reopened.findChildren<PgSession *>().isEmpty());
        QVERIFY(!reopened.findChild<QAction *>("runQueryAction")->isEnabled());
        QVERIFY(reopened.close());
        const auto blockedPath = temporary.filePath("blocked.ini");
        QVERIFY(QDir().mkpath(blockedPath));
        PgAdmMain blocked(nullptr, blockedPath);
        auto *blockedTabs = blocked.findChild<QTabWidget *>("queryTabs");
        auto *unsaved = qobject_cast<QueryTab *>(blockedTabs->currentWidget());
        unsaved->editor()->setPlainText("SELECT 'keep me';");
        QTimer::singleShot(0, &blocked, [] {
            if (auto *message = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                message->button(QMessageBox::Ok)->click();
        });
        QVERIFY(!blocked.close());
        QCOMPARE(unsaved->editor()->toPlainText(), QString("SELECT 'keep me';"));
    }

    void restoredTabConnections()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        QTemporaryDir temporary;
        const auto path = temporary.filePath("settings.ini");
        const auto serverId = profile().value("id").toString();
        const auto databaseTarget = serverId + '/' + QString::fromLatin1(database.toUtf8().toBase64(QByteArray::Base64UrlEncoding));
        auto other = profile();
        other["id"] = "other-" + suffix;
        other["name"] = "Other server";
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("connections/profiles", QVariantList{profile(), other});
            settings.setValue("editor/tabs", QVariantList{
                QVariantMap{{"target", databaseTarget}, {"sql", "SELECT 1/0;"}, {"tabName", "Legacy database tab"}},
                QVariantMap{{"target", serverId}, {"server", serverId}, {"database", "postgres"}, {"sql", "SELECT 1/0;"}},
                QVariantMap{{"target", other.value("id")}, {"sql", "SELECT 1/0;"}}});
        }
        PgAdmMain window(nullptr, path);
        auto *tabs = window.findChild<QTabWidget *>("queryTabs");
        QCOMPARE(tabs->count(), 3);
        auto *a = qobject_cast<QueryTab *>(tabs->widget(0));
        auto *b = qobject_cast<QueryTab *>(tabs->widget(1));
        auto *c = qobject_cast<QueryTab *>(tabs->widget(2));
        QCOMPARE(a->connectionId(), databaseTarget);
        QVERIFY(window.findChildren<PgSession *>().isEmpty());
        auto *tree = window.findChild<CatalogTree *>("catalogTree");
        tree->setCurrentItem(tree->serverItem(serverId));
        window.findChild<QAction *>("connectProfileAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(a->findChild<PgSession *>("querySession") &&
            a->findChild<PgSession *>("querySession")->state() == PgSession::Connected, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(b->findChild<PgSession *>("querySession") &&
            b->findChild<PgSession *>("querySession")->state() == PgSession::Connected, 10000);
        auto *aSession = a->findChild<PgSession *>("querySession");
        auto *bSession = b->findChild<PgSession *>("querySession");
        QCOMPARE(aSession->information().value("database").toString(), database);
        QCOMPARE(bSession->information().value("database").toString(), QString("postgres"));
        QVERIFY(aSession->information().value("backendPid") != bSession->information().value("backendPid"));
        QVERIFY(!c->findChild<PgSession *>("querySession"));
        QCOMPARE(tabs->count(), 3);
        QVERIFY(!a->isRunning() && !b->isRunning());
        QCOMPARE(a->findChild<QTableView *>("queryResults")->model()->rowCount(), 0);
        QCOMPARE(b->findChild<QTableView *>("queryResults")->model()->rowCount(), 0);
        tabs->setCurrentWidget(a);
        a->editor()->setPlainText("SELECT current_database();");
        window.findChild<QAction *>("runQueryAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!a->isRunning(), 5000);
        auto *model = qobject_cast<ResultModel *>(a->findChild<QTableView *>("queryResults")->model());
        QVERIFY(model->text().contains(database));
        QVERIFY(window.close());
        QSettings saved(path, QSettings::IniFormat);
        const auto snapshot = saved.value("editor/tabs").toList().first().toMap();
        QCOMPARE(snapshot.value("server").toString(), serverId);
        QCOMPARE(snapshot.value("database").toString(), database);
        QCOMPARE(snapshot.value("tabName").toString(), QString("Legacy database tab"));
    }

    void queryHistory()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        QTemporaryDir temporary;
        const auto settingsPath = temporary.filePath("settings.ini");
        {
            QSettings settings(settingsPath, QSettings::IniFormat);
            settings.setValue("connections/profiles", QVariantList{profile()});
            QVariantList old;
            for (int i = 0; i < 100; ++i) old.append(QVariantMap{{"sql", QString("old %1").arg(i)}});
            settings.setValue("editor/history", old);
        }
        {
            PgAdmMain window(nullptr, settingsPath);
            window.show();
            auto *tree = window.findChild<CatalogTree *>("catalogTree");
            tree->setCurrentItem(tree->topLevelItem(0));
            window.findChild<QAction *>("connectProfileAction")->trigger();
            auto *run = window.findChild<QAction *>("runQueryAction");
            QTRY_VERIFY_WITH_TIMEOUT(run->isEnabled(), 10000);
            auto *tabs = window.findChild<QTabWidget *>("queryTabs");
            auto *tab = qobject_cast<QueryTab *>(tabs->currentWidget());
            tab->editor()->setPlainText("SELECT 42; SELECT pg_sleep(30);");
            auto cursor = tab->editor()->textCursor();
            cursor.setPosition(0);
            cursor.setPosition(10, QTextCursor::KeepAnchor);
            tab->editor()->setTextCursor(cursor);
            run->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(!tab->isRunning(), 5000);
            auto *list = window.findChild<QListWidget *>("queryHistory");
            QCOMPARE(list->count(), 100);
            QCOMPARE(list->item(0)->data(Qt::UserRole).toMap().value("sql").toString(), QString("SELECT 42;"));
            QCOMPARE(list->item(99)->data(Qt::UserRole).toMap().value("sql").toString(), QString("old 98"));
            const int count = tabs->count();
            list->itemDoubleClicked(list->item(0));
            QCOMPARE(tabs->count(), count + 1);
            auto *copy = qobject_cast<QueryTab *>(tabs->currentWidget());
            QCOMPARE(copy->editor()->toPlainText(), QString("SELECT 42;"));
            QVERIFY(!copy->findChild<PgSession *>());
            QVERIFY(!copy->isRunning());
            tab->editor()->setPlainText("--" + QString(65000, 'x') + "\nSELECT 1;");
            tabs->setCurrentWidget(tab);
            run->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(!tab->isRunning(), 5000);
            QCOMPARE(list->count(), 100);
            QCOMPARE(list->item(0)->data(Qt::UserRole).toMap().value("sql").toString(), QString("SELECT 42;"));
            window.resize(1400, 850);
            window.findChild<QDockWidget *>("historyDock")->raise();
            QPixmap preview(window.size());
            window.render(&preview);
            QVERIFY(preview.save("editor-comfort-preview.png"));
            QVERIFY(window.close());
        }
        PgAdmMain reopened(nullptr, settingsPath);
        auto *list = reopened.findChild<QListWidget *>("queryHistory");
        QCOMPARE(list->count(), 100);
        QCOMPARE(list->item(0)->data(Qt::UserRole).toMap().value("sql").toString(), QString("SELECT 42;"));
        QVERIFY(reopened.findChildren<PgSession *>().isEmpty());
    }

    void profileDialogKeepsSecretsSeparate()
    {
        auto edited = profile();
        edited["port"] = 5432;
        edited["rememberPassword"] = true;
        const auto original = edited;
        QString secret = "previous";
        QTimer::singleShot(0, this, [] {
            if (auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget())) dialog->reject();
        });
        QVERIFY(!ProfileDialog::edit(nullptr, false, edited, secret));
        QCOMPARE(edited, original);
        QCOMPARE(secret, QString("previous"));
        QTimer::singleShot(0, this, [] {
            auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
            if (!dialog) return;
            for (auto *field : dialog->findChildren<QLineEdit *>())
                if (field->echoMode() == QLineEdit::Password) field->setText("dialog-test-only");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Save)->click();
        });
        QVERIFY(ProfileDialog::edit(nullptr, false, edited, secret));
        QCOMPARE(edited, original);
        QCOMPARE(secret, QString("dialog-test-only"));
        QVERIFY(!edited.contains("password"));
    }

    void qtKeychainRoundTrip()
    {
        keychainRoundTripStarted = true;
        PasswordStore store(nullptr, service);
        bool done = false;
        QString error, read;
        bool found = false;
        store.write(secretKey, QString::fromUtf8("test-only-€-wachtwoord"), [&](const QString &message) { error = message; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        done = false;
        store.read(secretKey, [&](bool exists, const QString &value, const QString &message) { found = exists; read = value; error = message; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(found);
        QCOMPARE(read, QString::fromUtf8("test-only-€-wachtwoord"));
        done = false;
        store.write(secretKey, "replacement-test-only", [&](const QString &message) { error = message; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(error.isEmpty());
        done = false;
        store.remove(secretKey, [&](const QString &message) { error = message; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(error.isEmpty());
        done = false;
        store.read(secretKey, [&](bool exists, const QString &, const QString &message) { found = exists; error = message; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        QVERIFY(!found);
        QVERIFY(error.isEmpty());
        auto changed = profile();
        const auto key = PasswordStore::key(changed);
        changed["database"] = "another_database";
        QCOMPARE(PasswordStore::key(changed), key);
        changed["host"] = "another_host";
        QVERIFY(PasswordStore::key(changed) != key);
    }

    void catalogsUseSeparateSessions()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        PgSession query;
        QSignalSpy connected(&query, &PgSession::connected);
        query.open(profile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QVERIFY(query.execute("SELECT pg_sleep(5);"));
        CatalogService catalog;
        QSignalSpy loaded(&catalog, &CatalogService::loaded);
        QSignalSpy failed(&catalog, &CatalogService::failed);
        catalog.request(profile(), {}, CatalogService::Databases);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 1, 3000);
        QVERIFY(query.isBusy());
        bool found = false;
        for (const auto &row : loaded.last().at(1).toList())
            if (row.toMap().value("name").toString() == database) found = true;
        QVERIFY(found);
        catalog.request(profile(), {}, CatalogService::Schemas);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 2, 5000);
        catalog.request(profile(), {}, CatalogService::Relations, schema);
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 3, 5000);
        QCOMPARE(loaded.last().at(1).toList().size(), 2);
        catalog.request(profile(), {}, CatalogService::Columns, schema, "voorbeeld");
        QTRY_COMPARE_WITH_TIMEOUT(loaded.count(), 4, 5000);
        const auto columns = loaded.last().at(1).toList();
        QCOMPARE(columns.size(), 2);
        QCOMPARE(columns.first().toMap().value("name").toString(), QString("id"));
        QCOMPARE(columns.first().toMap().value("not_null").toString(), QString("t"));
        QCOMPARE(failed.count(), 0);
        query.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!query.isBusy(), 5000);
    }

    void treeAndDatabaseQuery()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("settings.ini");
        auto savedProfile = profile();
        savedProfile["user"] = "pgadm_test_auth";
        savedProfile["rememberPassword"] = true;
        mainCredentialKey = PasswordStore::key(savedProfile);
        PasswordStore store;
        bool stored = false;
        QString storageError;
        store.write(mainCredentialKey, "temporary-test-only", [&](const QString &error) { storageError = error; stored = true; });
        QTRY_VERIFY_WITH_TIMEOUT(stored, 5000);
        QVERIFY2(storageError.isEmpty(), qPrintable(storageError));
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("connections/profiles", QVariantList{savedProfile});
            settings.sync();
        }
        QFile settingsFile(path);
        QVERIFY(settingsFile.open(QIODevice::ReadOnly));
        QVERIFY(!settingsFile.readAll().contains("temporary-test-only"));
        settingsFile.close();
        PgAdmMain window(nullptr, path);
        window.resize(1300, 850);
        auto *tree = window.findChild<CatalogTree *>("catalogTree");
        QVERIFY(tree);
        QCOMPARE(tree->topLevelItemCount(), 1);
        auto *root = tree->topLevelItem(0);
        QCOMPARE(root->child(0)->childCount(), 0); // No database work during startup.
        tree->setCurrentItem(root);
        auto *connect = window.findChild<QAction *>("connectProfileAction");
        QVERIFY(connect);
        connect->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(child(root->child(0), database), 10000);
        auto *postgres = child(root->child(0), "postgres");
        QVERIFY(postgres);
        postgres->setExpanded(true);
        QTRY_VERIFY_WITH_TIMEOUT(child(postgres, schema), 10000);
        auto *schemaItem = child(postgres, schema);
        schemaItem->setExpanded(true);
        QTRY_VERIFY_WITH_TIMEOUT(child(schemaItem, "Tabellen"), 10000);
        auto *table = child(child(schemaItem, "Tabellen"), "voorbeeld");
        QVERIFY(table);
        table->setExpanded(true);
        QTRY_COMPARE_WITH_TIMEOUT(table->childCount(), 2, 10000);
        QVERIFY(table->child(0)->text(0).contains("integer NOT NULL"));
        window.show();
        auto *detailsDock = window.findChild<QDockWidget *>("objectDetailsDock");
        QVERIFY(detailsDock && !detailsDock->isVisible());
        tree->setCurrentItem(table);
        auto *propertiesAction = window.findChild<QAction *>("objectPropertiesAction");
        QVERIFY(propertiesAction && propertiesAction->isEnabled());
        propertiesAction->trigger();
        QVERIFY(detailsDock->isVisible());
        auto *details = window.findChild<ObjectDetails *>();
        auto *properties = details->findChild<QTableWidget *>("objectProperties");
        QTRY_VERIFY_WITH_TIMEOUT(properties->rowCount() > 0, 5000);
        QCOMPARE(properties->item(0, 1)->text(), QString("voorbeeld"));
        tree->setCurrentItem(table->child(0));
        QTRY_VERIFY_WITH_TIMEOUT(properties->rowCount() > 0 && properties->item(0, 1)->text() == "id", 5000);
        detailsDock->hide();
        QCOMPARE(properties->rowCount(), 0);
        auto *db = child(root->child(0), database);
        tree->setCurrentItem(db);
        tree->itemDoubleClicked(db, 0);
        auto *tabs = window.findChild<QTabWidget *>("queryTabs");
        QVERIFY(tabs);
        QTRY_COMPARE_WITH_TIMEOUT(tabs->count(), 2, 10000);
        auto *query = qobject_cast<QueryTab *>(tabs->currentWidget());
        QVERIFY(query);
        QVERIFY(query->connectionId() != profile().value("id").toString());
        query->editor()->setPlainText("SELECT current_database();");
        QAction *run = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == "Selectie / script uitvoeren") run = action;
        QVERIFY(run && run->isEnabled());
        run->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!query->isRunning(), 5000);
        auto *view = query->findChild<QTableView *>("queryResults");
        auto *model = qobject_cast<ResultModel *>(view->model());
        QVERIFY(model->text().contains(database));
        window.show();
        QTest::qWait(50);
        QPixmap screenshot(window.size());
        window.render(&screenshot);
        QVERIFY(screenshot.save("database-browser-preview.png"));
        tree->setCurrentItem(root);
        auto *disconnect = window.findChild<QAction *>("disconnectProfileAction");
        QVERIFY(disconnect && disconnect->isEnabled());
        disconnect->trigger();
        QTRY_COMPARE_WITH_TIMEOUT(root->child(0)->childCount(), 0, 5000);
    }

    void postgresMaintenanceThenContextQuery()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        QTemporaryDir directory;
        const auto path = directory.filePath("settings.ini");
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue("connections/profiles", QVariantList{profile()});
            settings.sync();
        }
        PgAdmMain window(nullptr, path);
        window.show();
        auto *tree = window.findChild<CatalogTree *>("catalogTree");
        auto *tabs = window.findChild<QTabWidget *>("queryTabs");
        QVERIFY(tree && tabs);
        auto *root = tree->topLevelItem(0);
        tree->setCurrentItem(root);
        window.findChild<QAction *>("connectProfileAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(child(root->child(0), "lts_ontwikkel"), 10000);
        auto *db = child(root->child(0), "lts_ontwikkel");
        QVERIFY(triggerContextAction(window, tree, db));
        QTRY_COMPARE_WITH_TIMEOUT(tabs->count(), 2, 10000);
        auto *query = qobject_cast<QueryTab *>(tabs->currentWidget());
        const auto target = query->connectionId();
        QVERIFY(target != profile().value("id").toString());
        query->editor()->setPlainText("SELECT current_user, current_database();");
        QAction *run = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == "Selectie / script uitvoeren") run = action;
        QVERIFY(run && run->isEnabled());
        run->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!query->isRunning(), 5000);
        auto *view = query->findChild<QTableView *>("queryResults");
        auto *model = qobject_cast<ResultModel *>(view->model());
        QVERIFY(model->text().contains("lts_ontwikkel"));
        QVERIFY(model->text().contains("postgres"));
        // The context action remains usable when already connected.
        QVERIFY(triggerContextAction(window, tree, db));
        QCOMPARE(tabs->count(), 3);
        QCOMPARE(qobject_cast<QueryTab *>(tabs->currentWidget())->connectionId(), target);
        window.findChild<QAction *>("newQueryAction")->trigger();
        QCOMPARE(tabs->count(), 4);
        auto *extra = qobject_cast<QueryTab *>(tabs->currentWidget());
        QCOMPARE(extra->connectionId(), target);
        tree->setCurrentItem(db);
        window.findChild<QAction *>("disconnectProfileAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!run->isEnabled(), 5000);
        QCOMPARE(extra->connectionId(), target); // Do not silently switch back to postgres.
        QSettings saved(path, QSettings::IniFormat);
        QCOMPARE(saved.value("connections/profiles").toList().first().toMap().value("database").toString(), QString("postgres"));
    }

    // Exercise real catalog properties and pg_dump, including exact wildcard/quote
    // handling, rapid selection changes and permission failures on a disposable server.
    void objectPropertiesAndDefinition()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        auto quote = [](QString name) { name.replace('"', "\"\""); return '"' + name + '"'; };
        const QString table = "details\" *?";
        const QString view = "details_view";
        const auto qualified = quote(schema) + "." + quote(table);
        PgSession admin;
        QSignalSpy connected(&admin, &PgSession::connected);
        admin.open(profile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QSignalSpy finished(&admin, &PgSession::executionFinished);
        QVERIFY(admin.execute("CREATE TABLE " + qualified +
            " (id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY, body text DEFAULT 'ab''c'); "
            "CREATE INDEX details_body_idx ON " + qualified + "(body); COMMENT ON COLUMN " + qualified +
            ".body IS 'Column comment'; CREATE VIEW " + quote(schema) + "." + quote(view) +
            " AS SELECT body FROM " + qualified + ";"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(finished.first().at(0).toBool());

        CatalogService catalog;
        ObjectDetails inspector(&catalog);
        inspector.resize(1100, 500);
        inspector.show();
        auto *properties = inspector.findChild<QTableWidget *>("objectProperties");
        auto *tabs = inspector.findChild<QTabWidget *>("objectDetailTabs");
        auto *sql = inspector.findChild<QPlainTextEdit *>("objectDefinition");
        auto property = [properties](const QString &label) {
            for (int row = 0; row < properties->rowCount(); ++row)
                if (properties->item(row, 0)->text() == label) return properties->item(row, 1)->text();
            return QString();
        };
        inspector.load(profile(), {}, CatalogService::RelationProperties, schema, table);
        QTRY_COMPARE_WITH_TIMEOUT(property("Naam"), table, 5000);
        QCOMPARE(property("Eigenaar"), QString("postgres"));
        QVERIFY(inspector.findChildren<QProcess *>().isEmpty());
        tabs->setCurrentIndex(1);
        QTRY_VERIFY2_WITH_TIMEOUT(sql->toPlainText().contains("PRIMARY KEY"), qPrintable(sql->toPlainText()), 10000);
        QVERIFY(sql->toPlainText().contains("CREATE TABLE " + qualified));
        QVERIFY(sql->toPlainText().contains("CREATE INDEX"));
        QVERIFY(sql->toPlainText().contains("IDENTITY"));
        QVERIFY(sql->toPlainText().contains("Column comment"));
        QVERIFY(!sql->toPlainText().contains("COPY "));
        QVERIFY(!sql->toPlainText().contains("OWNER TO"));
        QTest::qWait(50);
        QPixmap preview(inspector.size());
        inspector.render(&preview);
        QVERIFY(preview.save("object-definition-preview.png"));

        tabs->setCurrentIndex(0);
        inspector.load(profile(), {}, CatalogService::ColumnProperties, schema, table, "body");
        QTRY_COMPARE_WITH_TIMEOUT(property("Naam"), QString("body"), 5000);
        QCOMPARE(property("Type"), QString("text"));
        QCOMPARE(property("Commentaar"), QString("Column comment"));
        QVERIFY(property("DEFAULT").contains("ab''c"));
        inspector.load(profile(), {}, CatalogService::DatabaseProperties);
        QTRY_COMPARE_WITH_TIMEOUT(property("Naam"), QString("postgres"), 5000);
        QCOMPARE(property("Encoding"), QString("UTF8"));
        inspector.load(profile(), {}, CatalogService::SchemaProperties, schema);
        QTRY_COMPARE_WITH_TIMEOUT(property("Naam"), schema, 5000);

        // Queue a stale table reply, then select a view before either request completes.
        inspector.load(profile(), {}, CatalogService::RelationProperties, schema, table);
        inspector.load(profile(), {}, CatalogService::RelationProperties, schema, view);
        tabs->setCurrentIndex(1);
        QTRY_COMPARE_WITH_TIMEOUT(property("Naam"), view, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(sql->toPlainText().contains("CREATE VIEW"), 10000);
        QVERIFY(!sql->toPlainText().contains("CREATE TABLE"));

        auto restricted = profile();
        restricted["id"] = profile().value("id").toString() + "-restricted";
        restricted["user"] = "pgadm_test_auth";
        inspector.load(restricted, "temporary-test-only", CatalogService::RelationProperties, schema, table);
        QTRY_COMPARE_WITH_TIMEOUT(property("Naam"), table, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(sql->toPlainText().contains("mislukt"), 10000);
        finished.clear();
        QVERIFY(admin.execute("GRANT SELECT ON " + qualified + " TO pgadm_test_auth;"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(finished.first().at(0).toBool());
        inspector.load(restricted, "temporary-test-only", CatalogService::RelationProperties, schema, table);
        QTRY_VERIFY_WITH_TIMEOUT(sql->toPlainText().contains("PRIMARY KEY"), 10000);
        finished.clear();
        QVERIFY(admin.execute("BEGIN; LOCK TABLE " + qualified + " IN ACCESS EXCLUSIVE MODE;"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(finished.first().at(0).toBool());
        bool responsive = false;
        QTimer::singleShot(50, &inspector, [&] { responsive = true; });
        inspector.load(profile(), {}, CatalogService::RelationProperties, schema, table);
        QTRY_VERIFY_WITH_TIMEOUT(responsive, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(sql->toPlainText().contains("mislukt"), 7000);
        finished.clear();
        QVERIFY(admin.execute("ROLLBACK;"));
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        inspector.load(profile(), {}, CatalogService::RelationProperties, schema, "missing_object");
        QTRY_VERIFY_WITH_TIMEOUT(sql->toPlainText().contains("mislukt"), 10000);
        inspector.clear();
        QTest::qWait(100);
        QVERIFY(sql->toPlainText().isEmpty());
        QCOMPARE(properties->rowCount(), 0);
    }

    // Protection and validation fail before a database tool is launched, even offline.
    void databaseJobValidation()
    {
        QVERIFY(!DatabaseJob::isArchive(QString()));
        DatabaseJob job;
        QSignalSpy finished(&job, &DatabaseJob::finished);
        job.drop(profile(), {}, "postgres");
        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(job.findChildren<QProcess *>().isEmpty());
        finished.clear();
        job.create(profile(), {}, "name\nwith-control");
        QCOMPARE(finished.count(), 1);
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(job.findChildren<QProcess *>().isEmpty());
        QVERIFY(!DatabaseJob::nameError(QString(64, 'x')).isEmpty());
        QVERIFY(DatabaseJob::nameError("quoted'=name").isEmpty());
        QVERIFY(!DatabaseJob::dropError({{"database", "maintenance"}}, "maintenance").isEmpty());
    }

    // Round-trip both archive and SQL backups, and verify cancellation/restore rollback
    // against newly created databases on the disposable PostgreSQL cluster only.
    void databaseBackupRestoreRoundTrip()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString sourceDatabase = "backup'=\"\\ " + suffix;
        const QString targetDatabase = "restored'= " + suffix;
        const QString sqlDatabase = "sql_restore_" + suffix;
        DatabaseJob job;
        QSignalSpy finished(&job, &DatabaseJob::finished);
        QString log;
        connect(&job, &DatabaseJob::output, this, [&](const QString &text) { log += text; });
        for (const auto &databaseName : {sourceDatabase, targetDatabase, sqlDatabase}) {
            finished.clear();
            job.create(profile(), {}, databaseName);
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
            QVERIFY2(finished.first().at(0).toBool(), qPrintable(log));
        }
        auto sourceProfile = profile();
        sourceProfile["database"] = sourceDatabase;
        PgSession source;
        QSignalSpy connected(&source, &PgSession::connected);
        QSignalSpy executed(&source, &PgSession::executionFinished);
        source.open(sourceProfile);
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QVERIFY(source.execute(QString::fromUtf8("CREATE TABLE sample (id bigint PRIMARY KEY, label text, payload bytea); "
            "INSERT INTO sample VALUES (9007199254740993, $msg$caf\xc3\xa9,\nregel$msg$, decode('00ff01', 'hex')); "
            "CREATE INDEX sample_label_idx ON sample(label); CREATE VIEW sample_view AS SELECT * FROM sample; "
            "CREATE SEQUENCE sample_seq START 42; SELECT nextval('sample_seq'); "
            "CREATE TABLE bulk_rows AS SELECT generate_series(1,25001) AS id;")));
        QTRY_COMPARE_WITH_TIMEOUT(executed.count(), 1, 10000);
        QVERIFY(executed.first().at(0).toBool());
        const auto archive = directory.filePath("full.backup");
        const auto sqlFile = directory.filePath("full.sql");
        for (bool plain : {false, true}) {
            finished.clear();
            job.backup(profile(), {}, sourceDatabase, plain ? sqlFile : archive, plain);
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
            QVERIFY2(finished.first().at(0).toBool(), qPrintable(log));
        }
        QVERIFY(DatabaseJob::isArchive(archive));
        QVERIFY(!DatabaseJob::isArchive(sqlFile));
        QFile saved(archive);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto original = saved.readAll();
        saved.close();
        for (bool plain : {false, true}) {
            finished.clear();
            job.restore(profile(), {}, plain ? sqlDatabase : targetDatabase, plain ? sqlFile : archive);
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
            QVERIFY2(finished.first().at(0).toBool(), qPrintable(log));
        }
        auto targetProfile = profile();
        targetProfile["database"] = targetDatabase;
        PgSession target;
        QSignalSpy targetConnected(&target, &PgSession::connected);
        QSignalSpy targetDone(&target, &PgSession::executionFinished);
        QSignalSpy result(&target, &PgSession::resultReady);
        target.open(targetProfile);
        QTRY_COMPARE_WITH_TIMEOUT(targetConnected.count(), 1, 5000);
        QVERIFY(target.execute("SELECT id, label, encode(payload,'hex'), (SELECT last_value FROM sample_seq), (SELECT count(*) FROM bulk_rows) FROM sample_view;"));
        QTRY_COMPARE_WITH_TIMEOUT(targetDone.count(), 1, 5000);
        QVector<ResultCell> row;
        for (const auto &batch : result)
            for (const auto &values : qvariant_cast<QueryResult>(batch.first()).rows) row = values;
        QCOMPARE(row.size(), 5);
        QCOMPARE(row[0].text, QString("9007199254740993"));
        QCOMPARE(row[1].text, QString::fromUtf8("caf\xc3\xa9,\nregel"));
        QCOMPARE(row[2].text, QString("00ff01"));
        QCOMPARE(row[3].text, QString("42"));
        QCOMPARE(row[4].text, QString("25001")); // Backup bypasses the query-view row limits.
        finished.clear();
        job.restore(profile(), {}, targetDatabase, archive);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
        QVERIFY(!finished.first().at(0).toBool()); // Existing objects fail rather than silently mixing data.
        finished.clear();
        job.restore(profile(), {}, targetDatabase, archive, true);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
        QVERIFY2(finished.first().at(0).toBool(), qPrintable(log));

        QFile invalidSql(directory.filePath("rollback.sql"));
        QVERIFY(invalidSql.open(QIODevice::WriteOnly));
        invalidSql.write("CREATE TABLE must_rollback(id int); SELECT 1/0;");
        invalidSql.close();
        finished.clear();
        job.restore(profile(), {}, targetDatabase, invalidSql.fileName());
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
        QVERIFY(!finished.first().at(0).toBool());
        targetDone.clear();
        result.clear();
        QVERIFY(target.execute("SELECT to_regclass('must_rollback') IS NULL, (SELECT count(*) FROM sample);"));
        QTRY_COMPARE_WITH_TIMEOUT(targetDone.count(), 1, 5000);
        row.clear();
        for (const auto &batch : result)
            for (const auto &values : qvariant_cast<QueryResult>(batch.first()).rows) row = values;
        QCOMPARE(row[0].text, QString("t"));
        QCOMPARE(row[1].text, QString("1"));

        auto restricted = profile();
        restricted["user"] = "pgadm_test_auth";
        finished.clear();
        job.backup(restricted, "temporary-test-only", sourceDatabase, archive);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 30000);
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), original);
        saved.close();
        executed.clear();
        QVERIFY(source.execute("BEGIN; LOCK TABLE sample IN ACCESS EXCLUSIVE MODE;"));
        QTRY_COMPARE_WITH_TIMEOUT(executed.count(), 1, 5000);
        finished.clear();
        job.backup(profile(), {}, sourceDatabase, archive);
        QTimer::singleShot(100, &job, &DatabaseJob::cancel);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(!finished.first().at(0).toBool());
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), original);
        saved.close();
        executed.clear();
        QVERIFY(source.execute("ROLLBACK;"));
        QTRY_COMPARE_WITH_TIMEOUT(executed.count(), 1, 5000);
        source.close();
        target.close();
        QTRY_COMPARE_WITH_TIMEOUT(source.state(), PgSession::Disconnected, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(target.state(), PgSession::Disconnected, 5000);
        for (const auto &databaseName : {sourceDatabase, targetDatabase, sqlDatabase}) {
            finished.clear();
            job.drop(profile(), {}, databaseName);
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
            QVERIFY2(finished.first().at(0).toBool(), qPrintable(log));
        }
    }

    // Exercise the actual server/database context menus, reviewed target, typed drop
    // confirmation, local session release and automatic tree refresh.
    void databaseContextTasks()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        QTemporaryDir directory;
        const auto settingsPath = directory.filePath("settings.ini");
        {
            QSettings settings(settingsPath, QSettings::IniFormat);
            settings.setValue("connections/profiles", QVariantList{profile()});
        }
        PgAdmMain window(nullptr, settingsPath);
        window.show();
        auto *tree = window.findChild<CatalogTree *>("catalogTree");
        auto *root = tree->topLevelItem(0);
        tree->setCurrentItem(root);
        QVERIFY(!window.findChild<QAction *>("createDatabaseAction")->isEnabled());
        window.findChild<QAction *>("connectProfileAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<QAction *>("createDatabaseAction")->isEnabled(), 10000);
        QVERIFY(triggerContextAction(window, tree, root, "manageRolesAction"));
        auto *roleDialog = window.findChild<RoleManager *>("roleManager");
        QVERIFY(roleDialog);
        QTRY_VERIFY_WITH_TIMEOUT(roleDialog->findChild<QPushButton *>("newRoleButton")->isEnabled(), 10000);
        delete roleDialog;
        auto *queryTabs = window.findChild<QTabWidget *>("queryTabs");
        auto *rootQuery = qobject_cast<QueryTab *>(queryTabs->currentWidget());
        rootQuery->editor()->setPlainText("BEGIN;");
        QAction *run = nullptr;
        for (auto *action : window.findChildren<QAction *>())
            if (action->text() == "Selectie / script uitvoeren") run = action;
        QVERIFY(run && run->isEnabled());
        run->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!rootQuery->isRunning(), 5000);
        const QString taskDatabase = "ui_task_" + suffix;
        QVERIFY(triggerContextAction(window, tree, root, "createDatabaseAction"));
        auto *dialog = window.findChild<DatabaseTaskDialog *>("databaseTaskDialog");
        QVERIFY(dialog);
        dialog->findChild<QLineEdit *>("databaseTaskName")->setText(taskDatabase);
        QSignalSpy created(dialog, &DatabaseTaskDialog::completed);
        dialog->findChild<QPushButton *>("databaseTaskStart")->click();
        QTRY_COMPARE_WITH_TIMEOUT(created.count(), 1, 30000);
        QVERIFY2(created.first().first().toBool(), qPrintable(dialog->findChild<QPlainTextEdit *>("databaseTaskLog")->toPlainText()));
        rootQuery->editor()->setPlainText("ROLLBACK;");
        run->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!rootQuery->isRunning(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(child(root->child(0), taskDatabase), 10000);
        auto *db = child(root->child(0), taskDatabase);
        QVERIFY(triggerContextAction(window, tree, db, "backupDatabaseAction"));
        dialog = window.findChild<DatabaseTaskDialog *>("databaseTaskDialog");
        const auto backup = directory.filePath("ui.backup");
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QTimer::singleShot(0, dialog, [&] {
            if (auto *chooser = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                chooser->selectFile(backup);
                QMetaObject::invokeMethod(chooser, "accept", Qt::DirectConnection);
            }
        });
        dialog->findChild<QPushButton *>("databaseTaskBrowse")->click();
        QCOMPARE(dialog->findChild<QLineEdit *>("databaseTaskFile")->text(), backup);
        QSignalSpy backedUp(dialog, &DatabaseTaskDialog::completed);
        QCOMPARE(dialog->findChild<QLabel *>("databaseTaskDuration")->text(), QString("Verstreken tijd: 00:00:00"));
        dialog->findChild<QPushButton *>("databaseTaskStart")->click();
        QVERIFY(dialog->findChild<QLabel *>("databaseTaskDuration")->text().startsWith("Verstreken tijd:"));
        QTRY_COMPARE_WITH_TIMEOUT(backedUp.count(), 1, 30000);
        QVERIFY(backedUp.first().first().toBool());
        QVERIFY(dialog->findChild<QLabel *>("databaseTaskDuration")->text().startsWith("Totale duur:"));
        QTRY_VERIFY_WITH_TIMEOUT(child(root->child(0), taskDatabase), 10000);
        db = child(root->child(0), taskDatabase);
        tree->setCurrentItem(db);
        window.findChild<QAction *>("openDatabaseQueryAction")->trigger();
        auto *tabs = window.findChild<QTabWidget *>("queryTabs");
        QTRY_COMPARE_WITH_TIMEOUT(tabs->count(), 2, 10000);
        auto *query = qobject_cast<QueryTab *>(tabs->currentWidget());
        const auto connection = query->connectionId();
        QVERIFY(triggerContextAction(window, tree, db, "restoreDatabaseAction"));
        dialog = window.findChild<DatabaseTaskDialog *>("databaseTaskDialog");
        dialog->findChild<QLineEdit *>("databaseTaskFile")->setText(backup);
        QSignalSpy restored(dialog, &DatabaseTaskDialog::completed);
        tree->setCurrentItem(root); // A different selection must not change the captured restore target.
        QCOMPARE(dialog->database(), taskDatabase);
        dialog->findChild<QPushButton *>("databaseTaskStart")->click();
        QTRY_COMPARE_WITH_TIMEOUT(restored.count(), 1, 30000);
        QVERIFY2(restored.first().first().toBool(), qPrintable(dialog->findChild<QPlainTextEdit *>("databaseTaskLog")->toPlainText()));
        QVERIFY(dialog->findChild<QLabel *>("databaseTaskDuration")->text().startsWith("Totale duur:"));
        QCOMPARE(query->connectionId(), connection);
        QTRY_VERIFY_WITH_TIMEOUT(child(root->child(0), taskDatabase), 10000);
        db = child(root->child(0), taskDatabase);
        QVERIFY(triggerContextAction(window, tree, db, "dropDatabaseAction"));
        dialog = window.findChild<DatabaseTaskDialog *>("databaseTaskDialog");
        auto *start = dialog->findChild<QPushButton *>("databaseTaskStart");
        QVERIFY(!start->isEnabled());
        auto *confirmation = dialog->findChild<QLineEdit *>("databaseDropConfirmation");
        confirmation->setText("wrong-name");
        QVERIFY(!start->isEnabled());
        confirmation->setText(taskDatabase);
        QVERIFY(start->isEnabled());
        QSignalSpy dropped(dialog, &DatabaseTaskDialog::completed);
        start->click();
        QTRY_COMPARE_WITH_TIMEOUT(dropped.count(), 1, 10000);
        QVERIFY2(dropped.first().first().toBool(), qPrintable(dialog->findChild<QPlainTextEdit *>("databaseTaskLog")->toPlainText()));
        QTRY_VERIFY_WITH_TIMEOUT(!tree->databaseNames(profile().value("id").toString()).contains(taskDatabase), 10000);
        QPixmap preview(dialog->size());
        dialog->render(&preview);
        QVERIFY(preview.save("database-task-preview.png"));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
    }

    void roleSqlValidation()
    {
        QString error;
        QVariantMap role{{"name", "owner\"';\\ test"}, {"comment", "quote'\\comment"}};
        const auto sql = RoleSql::roleChange(role, {}, &error);
        QVERIFY(error.isEmpty());
        QVERIFY(sql.contains("CREATE ROLE \"owner\"\"';\\ test\";"));
        QVERIFY(sql.contains("E'quote''\\\\comment'"));
        QVERIFY(!sql.contains("SUPERUSER"));
        QVERIFY(RoleSql::roleChange(role, role, &error).isEmpty());
        QVERIFY(RoleSql::nameError("pg_test").size());
        QVERIFY(RoleSql::nameError(QString(64, 'x')).size());
        QVariantMap before{{"CONNECT",true},{"CONNECT_grant",true}};
        QVariantMap after{{"CONNECT",true},{"CONNECT_grant",false}};
        QCOMPARE(RoleSql::databaseChange("owner", "db\"x", after, before, false, &error),
                 QString("REVOKE GRANT OPTION FOR CONNECT ON DATABASE \"db\"\"x\" FROM \"owner\";"));
    }

    void rolesAndSqlRestore()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        QTemporaryDir directory;
        const QString role = "pgadm_test_role_password";
        const QString parent = "restore_group_" + suffix;
        const QString table = "restore_probe_" + suffix;
        const QString fileName = directory.filePath("missing-owner.sql");
        QFile file(fileName);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(("CREATE TABLE " + table + " (id integer);\nALTER TABLE " + table + " OWNER TO " + role + ";\n").toUtf8());
        file.close();
        DatabaseJob job;
        QSignalSpy jobDone(&job, &DatabaseJob::finished);
        job.restore(profile(), {}, database, fileName, false, true);
        QTRY_COMPARE_WITH_TIMEOUT(jobDone.count(), 1, 10000);
        QVERIFY(!jobDone.first().first().toBool());

        PgSession admin;
        QSignalSpy connected(&admin, &PgSession::connected);
        admin.open(profile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QSignalSpy done(&admin, &PgSession::executionFinished);
        QSignalSpy results(&admin, &PgSession::resultReady);
        QVERIFY(admin.execute("CREATE ROLE " + parent + ";"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        QVERIFY(done.first().first().toBool());

        auto *manager = new RoleManager(profile(), {}, database, role);
        manager->show();
        auto *apply = manager->findChild<QPushButton *>("roleApplyButton");
        auto *name = manager->findChild<QLineEdit *>("roleName");
        auto *password = manager->findChild<QLineEdit *>("rolePassword");
        auto *preview = manager->findChild<QPlainTextEdit *>("roleSqlPreview");
        auto *login = manager->findChild<QCheckBox *>("role_login");
        auto *tabs = manager->findChild<QTabWidget *>("roleTabs");
        auto *memberships = manager->findChild<QListWidget *>("roleMemberships");
        QSignalSpy saved(manager, &RoleManager::rolesChanged);
        QTRY_VERIFY_WITH_TIMEOUT(apply->isEnabled(), 10000);
        QCOMPARE(name->text(), role);
        QVERIFY(!login->isChecked());
        for (int i = 0; i < memberships->count(); ++i)
            if (memberships->item(i)->text() == parent) memberships->item(i)->setCheckState(Qt::Checked);
        done.clear();
        QVERIFY(admin.execute("DROP ROLE " + parent + ";"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        apply->click(); // Membership target disappeared after the catalog read.
        QTRY_VERIFY_WITH_TIMEOUT(manager->findChild<QLabel *>("roleStatus")->text().contains("niet opgeslagen"), 10000);
        QCOMPARE(saved.count(), 0);
        done.clear();
        results.clear();
        QVERIFY(admin.execute("SELECT EXISTS(SELECT FROM pg_roles WHERE rolname='" + role + "')::text;"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        bool exists = true;
        for (const auto &batch : results)
            for (const auto &values : qvariant_cast<QueryResult>(batch.first()).rows) exists = values.first().text == "true";
        QVERIFY(!exists); // CREATE ROLE was rolled back along with the failed membership grant.
        done.clear();
        QVERIFY(admin.execute("CREATE ROLE " + parent + ";"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        apply->click();
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 1, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(name->isReadOnly() && login->isEnabled(), 10000);
        jobDone.clear();
        job.restore(profile(), {}, database, fileName, false, true);
        QTRY_COMPARE_WITH_TIMEOUT(jobDone.count(), 1, 10000);
        QVERIFY(jobDone.first().first().toBool()); // First failure rolled back CREATE TABLE.

        const QString secret = "temporary-role-password-only";
        login->setChecked(true);
        password->setText(secret);
        for (int i = 0; i < memberships->count(); ++i)
            if (memberships->item(i)->text() == parent) memberships->item(i)->setCheckState(Qt::Checked);
        QVERIFY(!preview->toPlainText().contains(secret));
        tabs->setCurrentIndex(1);
        QCOMPARE(tabs->currentIndex(), 0); // Pending role edits cannot be lost by switching forms.
        apply->click();
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 2, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(login->isEnabled(), 10000);
        QVERIFY(password->text().isEmpty());
        done.clear();
        results.clear();
        QVERIFY(admin.execute("SELECT rolcanlogin::text, (rolpassword LIKE 'SCRAM-SHA-256$%')::text, "
            "pg_has_role('" + role + "','" + parent + "','MEMBER')::text FROM pg_authid WHERE rolname='" + role + "';"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        QVector<ResultCell> row;
        for (const auto &batch : results)
            for (const auto &values : qvariant_cast<QueryResult>(batch.first()).rows) row = values;
        QCOMPARE(row.size(), 3);
        for (const auto &cell : row) QCOMPARE(cell.text, QString("true"));

        auto authProfile = profile();
        authProfile["user"] = role;
        authProfile["database"] = database;
        PgSession auth;
        QSignalSpy authenticated(&auth, &PgSession::connected);
        auth.open(authProfile, secret);
        QTRY_COMPARE_WITH_TIMEOUT(authenticated.count(), 1, 5000);
        auth.close();
        QTRY_COMPARE_WITH_TIMEOUT(auth.state(), PgSession::Disconnected, 5000);
        QSignalSpy denied(&auth, &PgSession::failed);
        auth.open(authProfile, "incorrect-test-password");
        QTRY_COMPARE_WITH_TIMEOUT(denied.count(), 1, 5000);
        QCOMPARE(authenticated.count(), 1);

        tabs->setCurrentIndex(1);
        auto *rights = manager->findChild<QTableWidget *>("databaseRoleRights");
        QTRY_VERIFY_WITH_TIMEOUT(rights->isEnabled(), 5000);
        QCOMPARE(rights->item(0, 1)->checkState(), Qt::Unchecked);
        QCOMPARE(rights->item(0, 3)->text(), QString("Ja")); // CONNECT inherited from PUBLIC.
        rights->item(1, 2)->setCheckState(Qt::Checked); // CREATE with GRANT OPTION.
        apply->click();
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 3, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(rights->isEnabled(), 10000);
        QCOMPARE(rights->item(1, 1)->checkState(), Qt::Checked);
        QCOMPARE(rights->item(1, 2)->checkState(), Qt::Checked);
        rights->item(1, 1)->setCheckState(Qt::Unchecked);
        apply->click();
        QTRY_COMPARE_WITH_TIMEOUT(saved.count(), 4, 10000);
        QTRY_VERIFY_WITH_TIMEOUT(rights->isEnabled(), 10000);
        QCOMPARE(rights->item(1, 1)->checkState(), Qt::Unchecked);
        rights->item(1, 1)->setCheckState(Qt::Checked);
        rights->item(1, 1)->setCheckState(Qt::Unchecked);
        tabs->setCurrentIndex(0);
        QCOMPARE(tabs->currentIndex(), 0);
        login->setChecked(false);
        login->setChecked(true);
        tabs->setCurrentIndex(1);
        QCOMPARE(tabs->currentIndex(), 1); // Reverting an edit also clears the pending-edit guard.
        QPixmap image(manager->size());
        manager->render(&image);
        QVERIFY(image.save("role-manager-preview.png"));
        delete manager;

        auto restrictedProfile = profile();
        restrictedProfile["user"] = "pgadm_test_auth";
        auto *restricted = new RoleManager(restrictedProfile, "temporary-test-only", database, "denied_" + suffix);
        auto *restrictedApply = restricted->findChild<QPushButton *>("roleApplyButton");
        QSignalSpy restrictedSaved(restricted, &RoleManager::rolesChanged);
        QTRY_VERIFY_WITH_TIMEOUT(restrictedApply->isEnabled(), 10000);
        restrictedApply->click();
        QTRY_VERIFY_WITH_TIMEOUT(restricted->findChild<QLabel *>("roleStatus")->text().contains("niet opgeslagen"), 10000);
        QCOMPARE(restrictedSaved.count(), 0);
        QVERIFY(!restricted->isApplying());
        delete restricted;

        auto targetProfile = profile();
        targetProfile["database"] = database;
        PgSession target;
        QSignalSpy targetConnected(&target, &PgSession::connected);
        target.open(targetProfile);
        QTRY_COMPARE_WITH_TIMEOUT(targetConnected.count(), 1, 5000);
        QSignalSpy targetDone(&target, &PgSession::executionFinished);
        QVERIFY(target.execute("DROP TABLE " + table + ";"));
        QTRY_COMPARE_WITH_TIMEOUT(targetDone.count(), 1, 5000);
        QVERIFY(targetDone.first().first().toBool());
        done.clear();
        QVERIFY(admin.execute("DROP ROLE " + role + "; DROP ROLE " + parent + ";"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        QVERIFY(done.first().first().toBool());
    }

    void fullExportFromQueryTab()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        QTemporaryDir directory;
        const auto settingsPath = directory.filePath("settings.ini");
        { QSettings settings(settingsPath, QSettings::IniFormat); settings.setValue("connections/profiles", QVariantList{profile()}); }
        PgAdmMain window(nullptr, settingsPath);
        window.show();
        auto *tree = window.findChild<CatalogTree *>("catalogTree");
        tree->setCurrentItem(tree->topLevelItem(0));
        auto *csvAction = window.findChild<QAction *>("fullResultCsvAction");
        QVERIFY(!csvAction->isEnabled());
        window.findChild<QAction *>("connectProfileAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(csvAction->isEnabled(), 10000);
        auto *tabs = window.findChild<QTabWidget *>("queryTabs");
        auto *tab = qobject_cast<QueryTab *>(tabs->currentWidget());
        tab->setAllResults(false); // Full-file output still contains every statement.
        tab->editor()->setPlainText("CREATE TEMP TABLE full_ui (id int); INSERT INTO full_ui VALUES (1);\n-- @resultaat: Alle rijen\nSELECT generate_series(1,25001) AS id;\nSELECT count(*) AS eenmaal FROM full_ui;");
        auto *button = tab->findChild<QToolButton *>("fullResultExportButton");
        QVERIFY(button && button->isEnabled());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
        QTimer::singleShot(0, &window, [] { if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) dialog->reject(); });
        csvAction->trigger();
        QVERIFY(!tab->findChild<PgSession *>("querySession")); // Cancelled chooser executed nothing.
        const auto destination = directory.filePath("full-ui.csv");
        QTimer::singleShot(0, &window, [destination] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget())) {
                dialog->selectFile(destination);
                QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
            }
        });
        button->menu()->actions().first()->trigger();
        QVERIFY(tab->isRunning());
        QVERIFY(!button->isEnabled());
        auto *session = tab->findChild<PgSession *>("querySession");
        QVERIFY(session);
        QSignalSpy exported(session, &PgSession::exportFinished);
        QTRY_VERIFY_WITH_TIMEOUT(!tab->isRunning(), 15000);
        QCOMPARE(exported.count(), 1);
        QVERIFY(exported.first().first().toBool());
        QCOMPARE(exported.first().at(2).toLongLong(), 25002LL);
        QFile file(destination);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto bytes = file.readAll();
        QVERIFY(bytes.contains("Alle rijen"));
        QVERIFY(bytes.contains("\"25001\"\r\n"));
        QVERIFY(bytes.contains("eenmaal"));
        QVERIFY(bytes.contains("\"1\"\r\n"));
        auto *view = tab->findChild<QTableView *>("queryResults");
        auto *model = qobject_cast<ResultModel *>(view->model());
        QVERIFY(model->rowCount() < 20); // Only the last result is displayed.
        QVERIFY(button->isEnabled());
        QPixmap image(window.size());
        window.render(&image);
        QVERIFY(image.save("full-export-preview.png"));
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, false);
    }

    void maintenanceSqlValidation()
    {
        QString error;
        QCOMPARE(DatabaseJob::maintenanceSql(DatabaseJob::Vacuum, "postgres", "odd\"schema", "table';--", true, true, &error),
            QString("VACUUM (VERBOSE, ANALYZE) \"odd\"\"schema\".\"table';--\";"));
        QVERIFY(error.isEmpty());
        QCOMPARE(DatabaseJob::maintenanceSql(DatabaseJob::Analyze, "postgres", {}, {}, false, true, &error), QString("ANALYZE (VERBOSE);"));
        QCOMPARE(DatabaseJob::maintenanceSql(DatabaseJob::Reindex, "odd\"db", {}, {}, true, true, &error),
            QString("REINDEX (VERBOSE) DATABASE CONCURRENTLY \"odd\"\"db\";"));
        QVERIFY(DatabaseJob::maintenanceSql(DatabaseJob::Drop, "postgres", {}, {}, true, true, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(DatabaseJob::maintenanceSql(DatabaseJob::Vacuum, "postgres", "public", {}, true, true, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void independentQueryTabs()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        QTemporaryDir directory;
        const auto settingsPath = directory.filePath("settings.ini");
        { QSettings settings(settingsPath, QSettings::IniFormat); settings.setValue("connections/profiles", QVariantList{profile()}); }
        const auto table = "tab_isolation_" + suffix;
        PgSession admin;
        QSignalSpy connected(&admin, &PgSession::connected);
        admin.open(profile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QSignalSpy adminDone(&admin, &PgSession::executionFinished);
        QVERIFY(admin.execute("CREATE TABLE " + table + " (id integer);"));
        QTRY_COMPARE_WITH_TIMEOUT(adminDone.count(), 1, 5000);
        QVERIFY(adminDone.first().first().toBool());

        PgAdmMain window(nullptr, settingsPath);
        window.show();
        auto *tree = window.findChild<CatalogTree *>("catalogTree");
        tree->setCurrentItem(tree->topLevelItem(0));
        window.findChild<QAction *>("connectProfileAction")->trigger();
        auto *run = window.findChild<QAction *>("runQueryAction");
        QTRY_VERIFY_WITH_TIMEOUT(run->isEnabled(), 10000);
        auto *tabs = window.findChild<QTabWidget *>("queryTabs");
        auto *a = qobject_cast<QueryTab *>(tabs->currentWidget());
        QVERIFY(!a->findChild<PgSession *>("querySession"));
        a->editor()->setPlainText("BEGIN; CREATE TEMP TABLE private_tab (id int); SET application_name='private_a'; INSERT INTO " + table + " VALUES (1);");
        run->trigger();
        QPointer<PgSession> aSession = a->findChild<PgSession *>("querySession");
        QVERIFY(aSession);
        QTRY_VERIFY_WITH_TIMEOUT(!a->isRunning(), 10000);
        QVERIFY(aSession->hasOpenTransaction());
        const auto aPid = aSession->information().value("backendPid").toInt();

        window.findChild<QAction *>("newQueryAction")->trigger();
        auto *b = qobject_cast<QueryTab *>(tabs->currentWidget());
        QCOMPARE(b->connectionId(), a->connectionId());
        QVERIFY(!b->findChild<PgSession *>("querySession"));
        b->editor()->setPlainText("SELECT count(*), to_regclass('pg_temp.private_tab') IS NULL, current_setting('application_name') <> 'private_a' FROM " + table + ";");
        run->trigger();
        auto *bSession = b->findChild<PgSession *>("querySession");
        QVERIFY(bSession);
        QSignalSpy bResults(bSession, &PgSession::resultReady);
        QTRY_VERIFY_WITH_TIMEOUT(!b->isRunning(), 10000);
        QVector<ResultCell> row;
        for (const auto &batch : bResults)
            for (const auto &values : qvariant_cast<QueryResult>(batch.first()).rows) row = values;
        QCOMPARE(row.size(), 3);
        QCOMPARE(row[0].text, QString("0"));
        QCOMPARE(row[1].text, QString("t"));
        QCOMPARE(row[2].text, QString("t"));
        const auto bPid = bSession->information().value("backendPid").toInt();
        QVERIFY(aPid != bPid);
        QVERIFY(!bSession->hasOpenTransaction());

        tabs->setCurrentWidget(a);
        a->editor()->setPlainText("SELECT pg_sleep(30);");
        run->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(aSession->isBusy(), 5000);
        tabs->setCurrentWidget(b);
        QVERIFY(run->isEnabled());
        b->editor()->setPlainText("SELECT 42;");
        run->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!b->isRunning(), 5000);
        QVERIFY(a->isRunning());
        tabs->setCurrentWidget(a);
        window.findChild<QAction *>("cancelQueryAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!a->isRunning(), 5000);
        QVERIFY(aSession->hasOpenTransaction());
        QVERIFY(!bSession->hasOpenTransaction());

        a->editor()->document()->setModified(false);
        const int count = tabs->count();
        QTimer::singleShot(0, &window, [] { if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) dialog->button(QMessageBox::Cancel)->click(); });
        tabs->tabCloseRequested(tabs->indexOf(a));
        QCOMPARE(tabs->count(), count);
        QVERIFY(aSession);
        QTimer::singleShot(0, &window, [] { if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) dialog->button(QMessageBox::Yes)->click(); });
        tabs->tabCloseRequested(tabs->indexOf(a));
        QCOMPARE(tabs->count(), count - 1);
        QVERIFY(aSession.isNull());
        QCOMPARE(bSession->information().value("backendPid").toInt(), bPid);
        bResults.clear();
        tabs->setCurrentWidget(b);
        b->editor()->setPlainText("SELECT count(*) FROM " + table + ";");
        run->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!b->isRunning(), 5000);
        row.clear();
        for (const auto &batch : bResults)
            for (const auto &values : qvariant_cast<QueryResult>(batch.first()).rows) row = values;
        QCOMPARE(row.first().text, QString("0")); // Closing A rolled back only A's insert.
        QList<QueryTab *> extraTabs;
        for (int index = 0; index < 7; ++index) {
            window.findChild<QAction *>("newQueryAction")->trigger();
            auto *extra = qobject_cast<QueryTab *>(tabs->currentWidget());
            extraTabs.append(extra);
            extra->editor()->setPlainText("SELECT 1;");
            run->trigger();
            QTRY_VERIFY_WITH_TIMEOUT(!extra->isRunning(), 5000);
            QVERIFY(extra->findChild<PgSession *>("querySession"));
            extra->editor()->document()->setModified(false);
        }
        window.findChild<QAction *>("newQueryAction")->trigger();
        auto *limited = qobject_cast<QueryTab *>(tabs->currentWidget());
        limited->editor()->setPlainText("SELECT 2;");
        QTimer::singleShot(0, &window, [] { if (auto *dialog = qobject_cast<QMessageBox *>(QApplication::activeModalWidget())) dialog->accept(); });
        run->trigger();
        QVERIFY(!limited->findChild<PgSession *>("querySession"));
        QVERIFY(!limited->isRunning());
        tabs->tabCloseRequested(tabs->indexOf(extraTabs.first()));
        tabs->setCurrentWidget(limited);
        run->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(!limited->isRunning(), 5000);
        QVERIFY(limited->findChild<PgSession *>("querySession"));
        adminDone.clear();
        QVERIFY(admin.execute("DROP TABLE " + table + ";"));
        QTRY_COMPARE_WITH_TIMEOUT(adminDone.count(), 1, 5000);
        QVERIFY(adminDone.first().first().toBool());
    }

    void maintenanceTasks()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires disposable PostgreSQL 18.");
        const auto table = "maintenance_" + suffix;
        PgSession admin;
        QSignalSpy connected(&admin, &PgSession::connected);
        admin.open(profile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QSignalSpy done(&admin, &PgSession::executionFinished);
        QVERIFY(admin.execute("CREATE TABLE " + table + " (id integer PRIMARY KEY); INSERT INTO " + table + " SELECT generate_series(1,100); BEGIN;"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        QVERIFY(done.first().first().toBool());
        QVERIFY(admin.hasOpenTransaction());
        DatabaseJob job;
        QString log;
        connect(&job, &DatabaseJob::output, this, [&](const QString &message) { log += message; });
        QSignalSpy finished(&job, &DatabaseJob::finished);
        for (const auto operation : {DatabaseJob::Vacuum, DatabaseJob::Analyze}) {
            finished.clear();
            job.maintain(operation, profile(), {}, "postgres", "public", table);
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 15000);
            QVERIFY2(finished.first().first().toBool(), qPrintable(log));
            QVERIFY(admin.hasOpenTransaction()); // Maintenance did not use/commit this session.
        }
        done.clear();
        QVERIFY(admin.execute("ROLLBACK;"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        finished.clear();
        job.maintain(DatabaseJob::Reindex, profile(), {}, "postgres", "public", table);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 20000);
        QVERIFY2(finished.first().first().toBool(), qPrintable(log));

        done.clear();
        QVERIFY(admin.execute("BEGIN; LOCK TABLE " + table + " IN ACCESS EXCLUSIVE MODE;"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        finished.clear();
        job.maintain(DatabaseJob::Reindex, profile(), {}, "postgres", "public", table, true, false);
        QTest::qWait(250);
        QVERIFY(job.isRunning());
        job.cancel();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 5000);
        QVERIFY(!finished.first().first().toBool());
        QVERIFY(admin.hasOpenTransaction());
        done.clear();
        QVERIFY(admin.execute("ROLLBACK;"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        finished.clear();
        auto restricted = profile();
        restricted["user"] = "pgadm_test_auth";
        job.maintain(DatabaseJob::Reindex, restricted, "temporary-test-only", "postgres", "public", table, true, false);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10000);
        QVERIFY(!finished.first().first().toBool());

        QTemporaryDir directory;
        const auto settingsPath = directory.filePath("settings.ini");
        { QSettings settings(settingsPath, QSettings::IniFormat); settings.setValue("connections/profiles", QVariantList{profile()}); }
        PgAdmMain window(nullptr, settingsPath);
        window.show();
        auto *tree = window.findChild<CatalogTree *>("catalogTree");
        auto *root = tree->topLevelItem(0);
        tree->setCurrentItem(root);
        window.findChild<QAction *>("connectProfileAction")->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(child(root->child(0), "postgres"), 10000);
        auto *db = child(root->child(0), "postgres");
        db->setExpanded(true);
        QTRY_VERIFY_WITH_TIMEOUT(child(db, "public"), 10000);
        auto *publicSchema = child(db, "public");
        publicSchema->setExpanded(true);
        QTRY_VERIFY_WITH_TIMEOUT(child(publicSchema, "Tabellen"), 10000);
        auto *tables = child(publicSchema, "Tabellen");
        tables->setExpanded(true);
        QTRY_VERIFY_WITH_TIMEOUT(child(tables, table), 10000);
        QVERIFY(triggerContextAction(window, tree, child(tables, table), "vacuumAction"));
        auto *dialog = window.findChild<DatabaseTaskDialog *>("databaseTaskDialog");
        QVERIFY(dialog);
        QVERIFY(dialog->findChild<QPlainTextEdit *>("maintenanceSqlPreview")->toPlainText().contains("\"public\".\"" + table + "\""));
        tree->setCurrentItem(root);
        QSignalSpy completed(dialog, &DatabaseTaskDialog::completed);
        dialog->findChild<QPushButton *>("databaseTaskStart")->click();
        QTRY_COMPARE_WITH_TIMEOUT(completed.count(), 1, 15000);
        QVERIFY(completed.first().first().toBool());
        QVERIFY(dialog->findChild<QLabel *>("databaseTaskDuration")->text().startsWith("Totale duur:"));
        QPixmap image(dialog->size());
        dialog->render(&image);
        QVERIFY(image.save("maintenance-preview.png"));
        done.clear();
        QVERIFY(admin.execute("DROP TABLE " + table + ";"));
        QTRY_COMPARE_WITH_TIMEOUT(done.count(), 1, 5000);
        QVERIFY(done.first().first().toBool());
    }

    void cleanupTestCase()
    {
        bool done = false;
        if (keychainRoundTripStarted) {
            PasswordStore store(nullptr, service);
            store.remove(secretKey, [&](const QString &) { done = true; });
            QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        }
        if (!mainCredentialKey.isEmpty()) {
            PasswordStore mainStore;
            done = false;
            mainStore.remove(mainCredentialKey, [&](const QString &) { done = true; });
            QTRY_VERIFY_WITH_TIMEOUT(done, 5000);
        }
    }
};
QTEST_MAIN(BrowserTest)
#include "tst_browser.moc"
