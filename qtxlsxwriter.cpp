#include "qtxlsxwriter.h"
#include <QFileInfo>
#include <QFile>
#include <QSaveFile>
#include <xlsxwriter.h>

// Stage beside the destination so failed generation never replaces an existing export.
QtXlsxWriter::QtXlsxWriter(const QString &path)
    : path(path), staging(QFileInfo(path).absolutePath() + "/.pgadm-export-XXXXXX")
{
    if (!staging.isValid()) { error = staging.errorString(); return; }
    lxw_workbook_options options{};
    options.constant_memory = LXW_TRUE;
    workbook = workbook_new_opt(staging.filePath("result.xlsx").toUtf8().constData(), &options);
    if (!workbook) { error = "Het Excel-bestand kon niet worden voorbereid."; return; }
    sheet = workbook_add_worksheet(workbook, "Resultaten");
    normal = workbook_add_format(workbook);
    heading = workbook_add_format(workbook);
    if (!sheet || !normal || !heading) { error = "Het Excel-werkblad kon niet worden voorbereid."; return; }
    for (auto *format : {normal, heading}) {
        format_set_num_format(format, "@");
        format_set_align(format, LXW_ALIGN_VERTICAL_TOP);
    }
    format_set_bold(heading);
    format_set_bg_color(heading, 0xDEEAF5);
}

// Close an abandoned workbook only in staging; QTemporaryDir removes its unpublished output.
QtXlsxWriter::~QtXlsxWriter()
{
    if (workbook) workbook_close(workbook);
}

// Latch the first native error; callers only receive Qt strings and never C-library handles.
bool QtXlsxWriter::check(int result)
{
    if (result != LXW_NO_ERROR && error.isEmpty())
        error = "Excel-export mislukt: " + QString::fromUtf8(lxw_strerror(static_cast<lxw_error>(result)));
    return error.isEmpty();
}

// Translate view pixel widths to Excel character widths, with the existing default/clamps.
bool QtXlsxWriter::setColumnWidths(int columns, const QVector<int> &pixels)
{
    if (!error.isEmpty()) return false;
    if (!workbook) { error = "Het Excel-bestand is al afgesloten."; return false; }
    if (columns < 1 || columns > 16384) { error = "Het resultaat overschrijdt de kolomlimiet van Excel. Gebruik CSV."; return false; }
    if (!check(worksheet_set_column(sheet, 0, columns - 1, 28, nullptr))) return false;
    for (int column = 0; column < qMin(columns, int(pixels.size())); ++column)
        if (!check(worksheet_set_column(sheet, column, column, qBound(5.0, (pixels[column] - 5) / 7.0, 255.0), nullptr))) return false;
    return true;
}

// Append explicit string cells; constant-memory mode keeps only the current row in memory.
bool QtXlsxWriter::writeRow(const QStringList &values, bool isHeading)
{
    if (!error.isEmpty()) return false;
    if (!workbook) { error = "Het Excel-bestand is al afgesloten."; return false; }
    if (row >= 1048576 || values.size() > 16384) {
        error = "Het resultaat overschrijdt de rij- of kolomlimiet van Excel. Gebruik CSV.";
        return false;
    }
    for (int column = 0; column < values.size(); ++column) {
        if (values[column].size() > 32767 || values[column].contains(QChar(0))) {
            error = QString("Cel op rij %1, kolom %2 bevat meer dan 32.767 tekens of een NUL-teken. Gebruik CSV om de waarde volledig te bewaren.").arg(row + 1).arg(column + 1);
            return false;
        }
        if (!check(worksheet_write_string(sheet, row, column, values[column].toUtf8().constData(), isHeading ? heading : normal))) return false;
    }
    ++row;
    return true;
}

// Finalise ZIP output, then atomically copy it into the destination using public Qt APIs.
QString QtXlsxWriter::save(const std::atomic_bool *cancelled)
{
    if (!error.isEmpty()) return error;
    if (!workbook) return "Het Excel-bestand is al afgesloten.";
    const auto result = workbook_close(workbook);
    workbook = nullptr;
    if (!check(result)) return error;
    if (cancelled && cancelled->load()) return "Export geannuleerd; het vorige bestand is behouden.";
    QFile temporary(staging.filePath("result.xlsx"));
    if (!temporary.open(QIODevice::ReadOnly)) { error = temporary.errorString(); return error; }
    QSaveFile destination(path);
    if (!destination.open(QIODevice::WriteOnly)) { error = destination.errorString(); return error; }
    while (!temporary.atEnd()) {
        if (cancelled && cancelled->load()) return "Export geannuleerd; het vorige bestand is behouden.";
        const auto bytes = temporary.read(65536);
        if (temporary.error() != QFileDevice::NoError) { error = temporary.errorString(); return error; }
        if (destination.write(bytes) != bytes.size()) { error = destination.errorString(); return error; }
    }
    if (cancelled && cancelled->load()) return "Export geannuleerd; het vorige bestand is behouden.";
    if (!destination.commit()) error = destination.errorString();
    return error;
}
