#include "sqlscript.h"
#include <QCoreApplication>
#include <QRegularExpression>

// Scan statement boundaries outside quoted strings/bodies and nested comments, retaining result annotations.
QVector<SqlStatement> SqlScript::split(const QString &sql, QString *error)
{
    error->clear();
    QVector<SqlStatement> statements;
    int start = 0;
    bool hasSql = false;
    QString name;
    QString keyword;
    auto identifier = [](QChar c) { return c.isLetterOrNumber() || c == '_' || c == '$'; };
    for (int i = 0; i < sql.size();) {
        const auto c = sql.at(i);
        const auto next = i + 1 < sql.size() ? sql.at(i + 1) : QChar();
        if (c.isSpace()) { ++i; continue; }
        if (c == '-' && next == '-') {
            int end = sql.indexOf('\n', i);
            if (end < 0) end = sql.size();
            if (!hasSql) {
                static const QRegularExpression annotation("^--\\s*@resultaat\\s*:\\s*(.+)$", QRegularExpression::CaseInsensitiveOption);
                const auto match = annotation.match(sql.mid(i, end - i).trimmed());
                if (match.hasMatch()) name = match.captured(1).trimmed();
            }
            i = end;
            continue;
        }
        if (c == '/' && next == '*') {
            int depth = 1;
            i += 2;
            while (i < sql.size() && depth) {
                if (sql.mid(i, 2) == "/*") { ++depth; i += 2; }
                else if (sql.mid(i, 2) == "*/") { --depth; i += 2; }
                else ++i;
            }
            if (depth) { *error = QCoreApplication::translate("sqlscript", "Niet-afgesloten blokcommentaar."); return {}; }
            continue;
        }
        if (c == ';') {
            if (hasSql) statements.append({sql.mid(start, i - start + 1), name, keyword});
            start = ++i;
            hasSql = false;
            name.clear();
            keyword.clear();
            continue;
        }
        if (c == '\'' || c == '"') {
            const bool escapes = c == '\'' && i > 0 && sql.at(i - 1).toLower() == 'e'
                && (i < 2 || !identifier(sql.at(i - 2)));
            const auto quote = c;
            hasSql = true;
            bool closed = false;
            ++i;
            while (i < sql.size()) {
                if (escapes && sql.at(i) == '\\') { i += 2; continue; }
                if (sql.at(i) == quote) {
                    if (i + 1 < sql.size() && sql.at(i + 1) == quote) { i += 2; continue; }
                    ++i;
                    closed = true;
                    break;
                }
                ++i;
            }
            if (!closed) { *error = "Niet-afgesloten SQL-string of identifier."; return {}; }
            continue;
        }
        if (c == '$' && (i == 0 || !identifier(sql.at(i - 1)))) {
            static const QRegularExpression delimiter("^\\$(?:[A-Za-z_][A-Za-z_0-9]*)?\\$");
            const auto match = delimiter.match(sql.mid(i));
            if (match.hasMatch()) {
                const auto tag = match.captured();
                const int end = sql.indexOf(tag, i + tag.size());
                if (end < 0) { *error = "Niet-afgesloten dollar-quoted SQL-string."; return {}; }
                i = end + tag.size();
                hasSql = true;
                continue;
            }
        }
        if (!hasSql && c.isLetter()) {
            int end = i + 1;
            while (end < sql.size() && identifier(sql.at(end))) ++end;
            keyword = sql.mid(i, end - i).toUpper();
        }
        hasSql = true;
        ++i;
    }
    if (hasSql) statements.append({sql.mid(start), name, keyword});
    return statements;
}
