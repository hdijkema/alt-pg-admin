#include "resultmodel.h"
#include <QBrush>
#include <QFontDatabase>
#include <QPalette>

// Represent the stored value as NULL, raw text or PostgreSQL-style bytea hex.
QString ResultCell::fullText() const
{
    if (isNull) return "NULL";
    return isBinary ? "\\x" + QString::fromLatin1(binary.toHex()) : text;
}

// Render a bounded one-line preview without changing the separately stored full value.
QString ResultCell::preview() const
{
    if (isNull) return "NULL";
    if (isBinary) return QString("[binair: %1 bytes%2] %3%4")
        .arg(sourceBytes).arg(truncated ? ", ingekort" : "")
        .arg(QString::fromLatin1(binary.left(24).toHex())).arg(binary.size() > 24 ? "…" : "");
    QString value = text.left(200);
    value.replace('\n', QChar(0x21b5));
    value.replace('\r', ' ');
    value.replace('\t', QChar(0x2192));
    if (text.size() > 200 || truncated) value += "…";
    return value;
}

ResultModel::ResultModel(QObject *parent) : QAbstractTableModel(parent) {}
int ResultModel::rowCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : rows.size(); }
int ResultModel::columnCount(const QModelIndex &parent) const { return parent.isValid() ? 0 : width; }

// Map flat row references to section labels, previews and contextual styling without per-cell widgets.
QVariant ResultModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= rows.size() || index.column() >= width) return {};
    const auto &row = rows[index.row()];
    const auto &section = sections[row.section];
    if (role == Qt::DisplayRole) {
        if (row.kind == Heading) return index.column() == 0 ? QString("%1. %2").arg(section.number).arg(section.title) : QVariant();
        if (row.kind == Columns) return section.columns.value(index.column());
        if (row.kind == Status) return index.column() == 0 ? section.status : QVariant();
        const auto *value = cell(index);
        return value ? value->preview() : QVariant();
    }
    if (role == Qt::FontRole) {
        auto font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setBold(row.kind == Heading || row.kind == Columns);
        return font;
    }
    if (role == Qt::BackgroundRole && (row.kind == Heading || row.kind == Columns))
        return QBrush(row.kind == Heading ? QColor(222, 234, 245) : QColor(240, 243, 246));
    if (role == Qt::ForegroundRole && (row.kind == Heading || row.kind == Columns)) return QBrush(QColor(25, 35, 45));
    if (role == Qt::ForegroundRole && row.kind == Status && section.error) return QBrush(QColor(190, 40, 40));
    if (role == Qt::ToolTipRole) {
        if (const auto *value = cell(index)) return QString("%1 bytes%2 — klik voor details, dubbelklik voor dialoog")
            .arg(value->sourceBytes).arg(value->truncated ? " (niet volledig opgeslagen)" : "");
        if (row.kind == Status) return section.status;
    }
    return {};
}

// Expose positional column headers because individual result sections may have different column names.
QVariant ResultModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole) return {};
    return orientation == Qt::Vertical ? QVariant(section + 1) : QVariant(QString::number(section + 1));
}

// Discard stored sections and row references inside one Qt model reset.
void ResultModel::clear()
{
    beginResetModel();
    sections.clear();
    rows.clear();
    width = 1;
    endResetModel();
}

// Retain ordered statement sections and insert row references for labels, values and final status.
int ResultModel::append(const QueryResult &batch, bool allResults)
{
    const bool existing = !sections.isEmpty() && sections.last().number == batch.number;
    if (!existing && !allResults) clear();
    const int firstInsertedRow = rows.size();
    if (!existing) {
        if (batch.columns.size() > width) {
            beginInsertColumns({}, width, batch.columns.size() - 1);
            width = batch.columns.size();
            endInsertColumns();
        }
        QueryResult section = batch;
        section.rows.clear();
        sections.append(section);
        const int count = batch.columns.isEmpty() ? 1 : 2;
        beginInsertRows({}, rows.size(), rows.size() + count - 1);
        rows.append({int(sections.size() - 1), Heading});
        if (!batch.columns.isEmpty()) rows.append({int(sections.size() - 1), Columns});
        endInsertRows();
    }
    auto &section = sections.last();
    const int count = batch.rows.size() + (batch.finished ? 1 : 0);
    if (count) {
        beginInsertRows({}, rows.size(), rows.size() + count - 1);
        for (const auto &value : batch.rows) {
            rows.append({int(sections.size() - 1), Values, int(section.rows.size())});
            section.rows.append(value);
        }
        if (batch.finished) rows.append({int(sections.size() - 1), Status});
        endInsertRows();
    }
    section.status = batch.status;
    section.finished = batch.finished;
    section.error = batch.error;
    section.truncated |= batch.truncated;
    section.elapsedMs = batch.elapsedMs;
    return firstInsertedRow;
}

// Resolve only value rows to stored cells; structural or absent cells return nullptr.
const ResultCell *ResultModel::cell(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= rows.size()) return nullptr;
    const auto &row = rows[index.row()];
    if (row.kind != Values) return nullptr;
    const auto &values = sections[row.section].rows[row.valueRow];
    return index.column() < values.size() ? &values[index.column()] : nullptr;
}

// Identify section/status rows that the table should span across visible columns.
bool ResultModel::isHeading(int row) const
{
    return row >= 0 && row < rows.size() && (rows[row].kind == Heading || rows[row].kind == Status);
}

// Build a clipboard overview from stored values, quoting TSV fields and marking incomplete output.
QString ResultModel::text() const
{
    QString output;
    auto field = [](QString text) {
        if (text.contains('\t') || text.contains('\n') || text.contains('\r') || text.contains('"')) {
            text.replace("\"", "\"\"");
            return '"' + text + '"';
        }
        return text;
    };
    for (const auto &section : sections) {
        output += QString("%1. %2\n").arg(section.number).arg(section.title);
        if (!section.columns.isEmpty()) {
            QStringList names;
            for (const auto &name : section.columns) names.append(field(name));
            output += names.join('\t') + '\n';
        }
        for (const auto &row : section.rows) {
            QStringList values;
            for (const auto &value : row) {
                const auto text = value.fullText() + (value.truncated ? " [INGEKORT]" : "");
                values.append(section.columns.size() == 1 && section.columns.first() == "QUERY PLAN" ? text : field(text));
            }
            output += values.join('\t') + '\n';
        }
        output += section.status + "\n\n";
        if (section.truncated) output += "[INGEKORT: alleen de opgeslagen resultaten zijn gekopieerd]\n\n";
    }
    return output;
}

// Find the most recent QUERY PLAN section and join its full text without changing indentation.
QString ResultModel::lastPlan() const
{
    for (auto it = sections.crbegin(); it != sections.crend(); ++it) {
        if (it->columns.size() == 1 && it->columns.first() == "QUERY PLAN") {
            QStringList lines;
            for (const auto &row : it->rows) lines.append(row.first().fullText());
            return lines.join('\n');
        }
    }
    return {};
}
