#include "resultexporter.h"
#include <QCoreApplication>
#include <QSaveFile>
#include <functional>
#include "qtxlsxwriter.h"

namespace {
QString cellText(const ResultCell &cell)
{
    return cell.fullText() + (cell.truncated ? QCoreApplication::translate("resultexporter", " [INGEKORT]") : "");
}

// A single result is a plain table; multiple results include their section labels.
bool visit(const QVector<QueryResult> &results, bool overview,
           const std::function<bool(const QStringList &, bool)> &row)
{
    for (const auto &section : results) {
        if (overview && !row({QString("%1. %2").arg(section.number).arg(section.title)}, true)) return false;
        if (!section.columns.isEmpty() && !row(section.columns, true)) return false;
        for (const auto &cells : section.rows) {
            QStringList values;
            for (const auto &cell : cells) values.append(cellText(cell));
            if (!row(values, false)) return false;
        }
        if ((overview || section.columns.isEmpty()) && !section.status.isEmpty() && !row({section.status}, false)) return false;
        if (section.truncated && !row({QCoreApplication::translate("resultexporter", "[INGEKORT: dit bestand bevat alleen de opgeslagen resultaten]")}, false)) return false;
        if (overview && !row({}, false)) return false;
    }
    return true;
}
}

// Inspect both section flags and stored cells before allowing an apparently complete export.
bool ResultExporter::isTruncated(const QVector<QueryResult> &results)
{
    for (const auto &section : results) {
        if (section.truncated) return true;
        for (const auto &row : section.rows)
            for (const auto &cell : row) if (cell.truncated) return true;
    }
    return false;
}

// Stream quoted UTF-8 rows with a BOM into QSaveFile; commit only after every row succeeds.
QString ResultExporter::csv(const QString &path, const QVector<QueryResult> &results)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    // UTF-8 BOM makes non-ASCII text recognisable when opened directly in Excel.
    if (file.write("\xEF\xBB\xBF", 3) != 3) return file.errorString();
    const bool written = visit(results, results.size() > 1, [&file](const QStringList &values, bool) {
        QStringList fields;
        for (auto value : values) {
            value.replace('"', "\"\"");
            fields.append('"' + value + '"');
        }
        const auto line = (fields.join(',') + "\r\n").toUtf8();
        return file.write(line) == line.size();
    });
    if (!written || !file.commit()) return file.errorString();
    return {};
}

// Traverse result sections; the Qt wrapper owns XLSX formatting, limits and safe publication.
QString ResultExporter::xlsx(const QString &path, const QVector<QueryResult> &results, const QVector<int> &widths)
{
    QtXlsxWriter book(path);
    int columns = 1;
    for (const auto &result : results) columns = qMax(columns, int(result.columns.size()));
    if (!book.setColumnWidths(columns, widths)) return book.errorString();
    if (!visit(results, true, [&book](const QStringList &values, bool heading) {
        return book.writeRow(values, heading);
    })) return book.errorString();
    return book.save();
}

// Escape every stored value into an overview table suitable for clipboard pasting.
QString ResultExporter::html(const QVector<QueryResult> &results)
{
    QString text = "<html><head><meta charset=\"utf-8\"></head><body><table>";
    visit(results, true, [&text](const QStringList &values, bool bold) {
        text += "<tr>";
        for (const auto &value : values) {
            text += "<td style=\"white-space:pre-wrap;mso-number-format:'\\@'\">";
            if (bold) text += "<b>";
            auto escaped = value.toHtmlEscaped();
            escaped.replace('\n', "<br>");
            text += escaped;
            if (bold) text += "</b>";
            text += "</td>";
        }
        text += "</tr>";
        return true;
    });
    return text + "</table></body></html>";
}
