#include "fullresultexport.h"
#include "qtxlsxwriter.h"
#include <QSaveFile>
#include <QFileInfo>

// Prepare staging before SQL starts and report immediately detectable path errors first.
FullResultExport::FullResultExport(const QString &path, bool excel, const QVector<int> &widths, bool overview)
    : widths(widths), overview(excel || overview)
{
    if (QFileInfo(path).isDir() || path.contains(QChar(0))) { error = "Kies een geldig exportbestand, geen map."; return; }
    if (excel) {
        xlsx = std::make_unique<QtXlsxWriter>(path);
        error = xlsx->errorString();
    } else {
        csv = std::make_unique<QSaveFile>(path);
        if (!csv->open(QIODevice::WriteOnly) || csv->write("\xEF\xBB\xBF", 3) != 3) error = csv->errorString();
    }
}

FullResultExport::~FullResultExport() = default;

// Preserve section headings for scripts; a single CSV SELECT remains a conventional table.
bool FullResultExport::beginResult(int number, const QString &title, const QStringList &columns)
{
    hasColumns = !columns.isEmpty();
    if (!error.isEmpty()) return false;
    if (xlsx && !xlsx->setColumnWidths(qMax(1, int(columns.size())), widths)) { error = xlsx->errorString(); return false; }
    if (overview && !writeRow({QString("%1. %2").arg(number).arg(title)}, true)) return false;
    return columns.isEmpty() || writeRow(columns, true);
}

// Write explicit strings, preserving CSV quotes/newlines and the existing XLSX cell limits.
bool FullResultExport::writeRow(const QStringList &values, bool heading)
{
    if (!error.isEmpty()) return false;
    if (xlsx) {
        if (!xlsx->writeRow(values, heading)) error = xlsx->errorString();
    } else {
        QStringList fields;
        for (auto value : values) { value.replace('"', "\"\""); fields.append('"' + value + '"'); }
        const auto bytes = (fields.join(',') + "\r\n").toUtf8();
        if (csv->write(bytes) != bytes.size()) error = csv->errorString();
    }
    return error.isEmpty();
}

// Keep completion messages in overview files, without preview truncation markers.
bool FullResultExport::endResult(const QString &status)
{
    if (!overview) return hasColumns || status.isEmpty() ? error.isEmpty() : writeRow({status});
    return (status.isEmpty() || writeRow({status})) && writeRow({});
}

// Publish only after the complete script succeeds; never promise rollback of SQL side effects.
QString FullResultExport::finish(bool success, const std::atomic_bool *cancelled)
{
    if (!error.isEmpty()) return error;
    if (!success || cancelled->load()) return "Export niet afgerond; het vorige bestand is behouden.";
    if (xlsx) return xlsx->save(cancelled);
    if (!csv->commit()) return csv->errorString();
    return {};
}
