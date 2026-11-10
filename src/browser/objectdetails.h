#ifndef OBJECTDETAILS_H
#define OBJECTDETAILS_H
#include "database/catalogservice.h"
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QProcess;
class QPushButton;
class QTableWidget;
class QTabWidget;

// goal: Inspect one catalog object without modifying database objects or data.
// pre: The borrowed CatalogService is alive when load() is called; GUI-thread use.
// post: Only the latest selection may update the properties and definition views.
// internals: Properties share CatalogTree's metadata queue. Definition SQL is
//            requested lazily from a separate, bounded pg_dump child process.
class ObjectDetails : public QWidget
{
    Q_OBJECT
public:
    explicit ObjectDetails(CatalogService *catalog, QWidget *parent = nullptr);
    ~ObjectDetails() override;
    // Load properties immediately; fetch relation/parent-relation SQL only when
    // the SQL tab is selected. Password is held in memory and never saved here.
    void load(const QVariantMap &profile, const QString &password, CatalogService::Kind kind,
              const QString &schema = {}, const QString &relation = {}, const QString &column = {});
    // Cancel a definition process and invalidate outstanding property replies.
    void clear();

private:
    void showProperties(const QVariantList &rows);
    void startDump();
    void readDump();
    void stopDump();
    void saveSql();
    CatalogService *catalog;
    QLabel *title;
    QTabWidget *tabs;
    QTableWidget *properties;
    QPlainTextEdit *sql;
    QPushButton *copy;
    QPushButton *save;
    QVariantMap profile;
    QString password;
    QString schema;
    QString relation;
    quint64 request = 0;
    QProcess *dump = nullptr;
    QByteArray dumpOutput;
    QByteArray dumpWarnings;
    QString dumpError;
    bool dumpRequested = false;
};
#endif
