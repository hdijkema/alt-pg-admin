#ifndef QTXLSXWRITER_H
#define QTXLSXWRITER_H
#include <QStringList>
#include <QTemporaryDir>
#include <QVector>

struct lxw_workbook;
struct lxw_worksheet;
struct lxw_format;

// goal: Bridge Qt text rows and pixel widths to one XLSX result sheet.
// pre: One instance per export/thread. Append rows in order, then call save once.
// post: Values remain strings; errors preserve the destination. Destruction discards unfinished output.
// internals: Own libxlsxwriter handles and a private QTemporaryDir. Convert QString to UTF-8,
//            use two fixed formats, and publish the closed workbook through QSaveFile.
class QtXlsxWriter
{
public:
    explicit QtXlsxWriter(const QString &path);
    ~QtXlsxWriter();
    Q_DISABLE_COPY(QtXlsxWriter)
    bool setColumnWidths(int columns, const QVector<int> &pixels);
    bool writeRow(const QStringList &values, bool heading = false);
    QString save();
    QString errorString() const { return error; }
private:
    bool check(int result);
    QString path;
    QTemporaryDir staging;
    lxw_workbook *workbook = nullptr;
    lxw_worksheet *sheet = nullptr;
    lxw_format *normal = nullptr;
    lxw_format *heading = nullptr;
    QString error;
    int row = 0;
};
#endif
