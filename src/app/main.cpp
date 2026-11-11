#include "pgadmmain.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QIcon>
#include <QDebug>
#include <QDir>
#include <QLibraryInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QTranslator>

// Construct the native window and optionally record a bounded startup smoke check.
int main(int argc, char *argv[])
{
    QElapsedTimer startup;
    startup.start();
    QApplication a(argc, argv);
#ifdef Q_OS_WIN
    a.setWindowIcon(QIcon(":/icons/alt-pg-admin.ico"));
#else
    a.setWindowIcon(QIcon(":/icons/alt-pg-admin.svg"));
#endif
    a.setDesktopFileName("io.github.hdijkema.AltPgAdmin");
    QCoreApplication::setOrganizationName("Hans");
    QCoreApplication::setApplicationName("PgAdmQt");
    QCoreApplication::setApplicationVersion(PGADM_APP_VERSION);
    const auto arguments = a.arguments();
    const int settingsIndex = arguments.indexOf("--settings-file");
    const QString settingsPath = settingsIndex >= 0 && settingsIndex + 1 < arguments.size()
        ? arguments.at(settingsIndex + 1)
        : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) + "/settings.ini";
    QSettings settings(settingsPath, QSettings::IniFormat);
    QString language = settings.value("ui/language", "nl").toString();
    const int languageIndex = arguments.indexOf("--language");
    if (languageIndex >= 0) {
        if (languageIndex + 1 >= arguments.size()) return 1;
        language = arguments.at(languageIndex + 1);
    }
    if (language != "nl" && language != "en" && language != "de") {
        if (languageIndex >= 0) return 1;
        language = "nl";
    }
    QTranslator qtTranslator;
#ifdef Q_OS_MACOS
    const auto qtTranslationsPath = QCoreApplication::applicationDirPath() + "/../Resources/translations";
#else
    const auto qtTranslationsPath = QCoreApplication::applicationDirPath() + "/translations";
#endif
    if (qtTranslator.load("qt_" + language, qtTranslationsPath)
        || qtTranslator.load("qtbase_" + language, QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        a.installTranslator(&qtTranslator);
    QTranslator translator;
    if (language != "nl") {
        if (!translator.load(":/i18n/pgadmin_" + language + ".qm")) return 1;
        a.installTranslator(&translator);
    }
    a.setProperty("uiLanguage", language);
    PgAdmMain w;
    w.show();
    if (a.arguments().contains("--startup-check")) {
        QTimer::singleShot(0, &w, [&] {
            if (a.windowIcon().pixmap(32, 32).isNull()) {
                qCritical("Application icon could not be rendered. Check the Qt SVG/image plugins.");
                a.exit(1);
                return;
            }
            const int index = a.arguments().indexOf("--startup-report");
            if (index >= 0 && index + 1 < a.arguments().size()) {
                QFile report(a.arguments().at(index + 1));
                if (report.open(QIODevice::WriteOnly | QIODevice::Text)) {
                    report.write(QByteArray::number(startup.elapsed()) + " ms to first event-loop callback\n");
                    report.write("Application version: " + QCoreApplication::applicationVersion().toUtf8() + "\n");
                }
            }
            QTimer::singleShot(200, &a, &QApplication::quit);
        });
    }
    return a.exec();
}
