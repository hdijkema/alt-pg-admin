#ifndef FULLRESULTEXPORT_H
#define FULLRESULTEXPORT_H
#include <QStringList>
#include <QVector>
#include <atomic>
#include <memory>

class QSaveFile;
class QtXlsxWriter;

// goal: Stream complete result sections to CSV/XLSX without retaining earlier rows.
// pre: Use on one worker thread, in section/row order, then finish once.
// post: Only successful completion publishes a file. Errors/cancellation preserve the target.
// internals: QSaveFile streams CSV; QtXlsxWriter streams XLSX with its existing staging.
//            Preview limits never enter this class. Memory scales with one row, not row count.
class FullResultExport
{
public:
    FullResultExport(const QString &path, bool excel, const QVector<int> &widths, bool overview);
    ~FullResultExport();
    bool beginResult(int number, const QString &title, const QStringList &columns);
    bool writeRow(const QStringList &values, bool heading = false);
    bool endResult(const QString &status);
    QString finish(bool success, const std::atomic_bool *cancelled);
    QString errorString() const { return error; }
private:
    std::unique_ptr<QSaveFile> csv;
    std::unique_ptr<QtXlsxWriter> xlsx;
    QVector<int> widths;
    bool overview;
    bool hasColumns = false;
    QString error;
};
#endif
