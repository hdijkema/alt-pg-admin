#include "pgadmmain.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>

// Construct the native window and optionally record a bounded startup smoke check.
int main(int argc, char *argv[])
{
    QElapsedTimer startup;
    startup.start();
    QApplication a(argc, argv);
    QCoreApplication::setOrganizationName("Hans");
    QCoreApplication::setApplicationName("PgAdmQt");
    QCoreApplication::setApplicationVersion(PGADM_APP_VERSION);
    PgAdmMain w;
    w.show();
    if (a.arguments().contains("--startup-check")) {
        QTimer::singleShot(0, &w, [&] {
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
