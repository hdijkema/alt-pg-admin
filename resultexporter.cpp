#include "resultexporter.h"
#include <QSaveFile>
#include <QTemporaryFile>
#include <QFileInfo>
#include <functional>
#include "xlsxdocument.h"
#include "xlsxworksheet.h"
#include "xlsxformat.h"
#include "xlsxworkbook.h"

namespace {
QString cellText(const ResultCell &cell)
{
    return cell.fullText() + (cell.truncated ? " [INGEKORT]" : "");
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
        if (section.truncated && !row({"[INGEKORT: dit bestand bevat alleen de opgeslagen resultaten]"}, false)) return false;
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

// Preserve exact values as string cells, reject Excel limits, and stage QXlsx output before atomic replacement.
QString ResultExporter::xlsx(const QString &path, const QVector<QueryResult> &results, const QVector<int> &widths)
{
    QXlsx::Document book;
    book.workbook()->setHtmlToRichStringEnabled(false);
    book.addSheet("Resultaten");
    book.selectSheet("Resultaten");
    QXlsx::Format normal, heading;
    normal.setNumberFormat("@");
    normal.setVerticalAlignment(QXlsx::Format::AlignTop);
    heading = normal;
    heading.setFontBold(true);
    heading.setPatternBackgroundColor(QColor(222, 234, 245));
    int columns = 1;
    for (const auto &result : results) columns = qMax(columns, int(result.columns.size()));
    if (columns > 16384) return "Het resultaat overschrijdt de kolomlimiet van Excel. Gebruik CSV.";
    book.setColumnWidth(1, columns, 28);
    for (int column = 0; column < qMin(columns, int(widths.size())); ++column)
        book.setColumnWidth(column + 1, qBound(5.0, (widths[column] - 5) / 7.0, 255.0));
    int row = 0;
    QString error;
    const bool written = visit(results, true, [&](const QStringList &values, bool bold) {
        if (++row > 1048576 || values.size() > 16384) {
            error = "Het resultaat overschrijdt de rij- of kolomlimiet van Excel. Gebruik CSV."; return false;
        }
        for (int column = 0; column < values.size(); ++column) {
            if (values[column].size() > 32767) {
                error = QString("Cel op rij %1, kolom %2 bevat meer dan 32.767 tekens. Gebruik CSV om de waarde volledig te bewaren.").arg(row).arg(column + 1);
                return false;
            }
            // Explicit strings preserve large integers, leading zeroes and SQL text beginning with '='.
            if (!book.currentWorksheet()->writeString(row, column + 1, values[column], bold ? heading : normal)) {
                error = QString("Excel-cel op rij %1, kolom %2 kon niet worden geschreven.").arg(row).arg(column + 1);
                return false;
            }
        }
        return true;
    });
    if (!written) return error;
    // QXlsx closes its output device. QSaveFile must only be closed by commit(),
    // so stage the workbook in a temporary file, then copy it atomically.
    QTemporaryFile temporary(QFileInfo(path).absolutePath() + "/.pgadm-export-XXXXXX");
    if (!temporary.open()) return temporary.errorString();
    if (!book.saveAs(&temporary)) return "Het Excel-bestand kon niet worden geschreven.";
    if (!temporary.isOpen() && !temporary.open()) return temporary.errorString();
    if (!temporary.seek(0)) return temporary.errorString();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return file.errorString();
    while (!temporary.atEnd()) {
        const auto bytes = temporary.read(65536);
        if (temporary.error() != QFileDevice::NoError) return temporary.errorString();
        if (file.write(bytes) != bytes.size()) return file.errorString();
    }
    if (!file.commit()) return file.errorString();
    return {};
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
