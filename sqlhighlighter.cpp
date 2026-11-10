#include "sqlhighlighter.h"
#include <QTextBlock>
#include <QSet>
#include <QFont>

namespace {
struct SqlBlockState : QTextBlockUserData {
    int depth = 0;
    QChar quote;
    bool escapes = false;
    QString dollar;
};
bool wordStart(QChar c) { return c.isLetter() || c == '_'; }
bool wordPart(QChar c) { return c.isLetterOrNumber() || c == '_' || c == '$'; }
}

// Share the supported SQL vocabulary between highlighting and explicit completion.
QStringList sqlKeywords()
{
    const QString words = "ALL ALTER ANALYZE AND ANY ARRAY AS ASC ASYMMETRIC AUTHORIZATION BEGIN BETWEEN BIGINT "
            "BINARY BOOLEAN BOTH BY CALL CASCADE CASE CAST CHECK COLLATE COLUMN COMMENT COMMIT CONCURRENTLY CONFLICT "
            "CONSTRAINT COPY CREATE CROSS CURRENT_DATE CURRENT_TIME CURRENT_TIMESTAMP CURRENT_USER DATABASE DATE "
            "DECLARE DEFAULT DELETE DESC DISTINCT DO DROP ELSE END ESCAPE EXCEPT EXCLUDE EXECUTE EXISTS EXPLAIN "
            "FALSE FETCH FILTER FIRST FLOAT FOLLOWING FOR FOREIGN FROM FULL FUNCTION GENERATED GRANT GROUP HAVING "
            "IDENTITY IF ILIKE IN INCLUDE INDEX INHERITS INITIALLY INNER INSERT INTEGER INTERSECT INTERVAL INTO IS "
            "JOIN JSON JSONB KEY LANGUAGE LATERAL LEADING LEFT LIKE LIMIT LOCAL LOCK MATERIALIZED NATURAL NOT "
            "NOTHING NULL NULLS NUMERIC OF OFFSET ON ONLY OR ORDER OUTER OVER OWNER PARTITION PLACING PRECEDING "
            "PRIMARY PROCEDURE RANGE REAL RECURSIVE REFERENCES REFRESH REINDEX RELEASE RENAME REPLACE RESTRICT "
            "RETURN RETURNING RETURNS REVOKE RIGHT ROLE ROLLBACK ROW ROWS SAVEPOINT SCHEMA SELECT SEQUENCE SESSION "
            "SET SHOW SIMILAR SMALLINT SOME TABLE TABLESPACE TEMP TEMPORARY TEXT THEN TIES TIME TIMESTAMP TO "
            "TRAILING TRANSACTION TRIGGER TRUE TRUNCATE TYPE UNBOUNDED UNION UNIQUE UNKNOWN UPDATE USER USING "
            "VACUUM VALUES VARCHAR VARIADIC VERBOSE VIEW WHEN WHERE WINDOW WITH WITHIN WITHOUT ZONE";
    return words.split(' ');
}

// Attach concrete token formats to the document using Qt incremental highlighting.
SqlHighlighter::SqlHighlighter(QTextDocument *document) : QSyntaxHighlighter(document)
{
    keyword.setForeground(QColor(30, 75, 190));
    keyword.setFontWeight(QFont::Bold);
    string.setForeground(QColor(155, 55, 35));
    comment.setForeground(QColor(65, 125, 70));
    number.setForeground(QColor(125, 55, 160));
    identifier.setForeground(QColor(0, 110, 130));
}

// Scan tokens with carried quote/dollar/comment state so multiline text is not mistaken for SQL keywords.
void SqlHighlighter::highlightBlock(const QString &text)
{
    static const QSet<QString> keywords = [] {
        const auto list = sqlKeywords();
        return QSet<QString>(list.begin(), list.end());
    }();
    auto *state = new SqlBlockState;
    if (const auto *previous = dynamic_cast<SqlBlockState *>(currentBlock().previous().userData())) {
        state->depth = previous->depth;
        state->quote = previous->quote;
        state->escapes = previous->escapes;
        state->dollar = previous->dollar;
    }
    int i = 0;
    while (i < text.size()) {
        const int start = i;
        if (state->depth || (state->quote.isNull() && state->dollar.isEmpty() && text.mid(i, 2) == "/*")) {
            if (!state->depth) { state->depth = 1; i += 2; }
            while (i < text.size() && state->depth) {
                if (text.mid(i, 2) == "/*") { ++state->depth; i += 2; }
                else if (text.mid(i, 2) == "*/") { --state->depth; i += 2; }
                else ++i;
            }
            setFormat(start, i - start, comment);
        } else if (!state->dollar.isEmpty()) {
            const int end = text.indexOf(state->dollar, i);
            i = end < 0 ? text.size() : end + state->dollar.size();
            if (end >= 0) state->dollar.clear();
            setFormat(start, i - start, string);
        } else if (!state->quote.isNull() || text[i] == '\'' || text[i] == '"'
                   || ((text[i] == 'E' || text[i] == 'e') && i + 1 < text.size() && text[i + 1] == '\'')) {
            if (state->quote.isNull()) {
                state->escapes = text[i] == 'E' || text[i] == 'e';
                if (state->escapes) ++i;
                state->quote = text[i++];
            }
            const bool quotedIdentifier = state->quote == '"';
            while (i < text.size()) {
                if (state->escapes && text[i] == '\\') { i = qMin(i + 2, int(text.size())); continue; }
                if (text[i++] != state->quote) continue;
                if (i < text.size() && text[i] == state->quote) { ++i; continue; }
                state->quote = QChar(); state->escapes = false; break;
            }
            setFormat(start, i - start, quotedIdentifier ? identifier : string);
        } else if (text.mid(i, 2) == "--") {
            setFormat(i, text.size() - i, comment); break;
        } else if (text[i] == '$') {
            int end = i + 1;
            if (end < text.size() && wordStart(text[end]))
                while (end < text.size() && (text[end].isLetterOrNumber() || text[end] == '_')) ++end;
            if (end < text.size() && text[end] == '$') {
                state->dollar = text.mid(i, end - i + 1);
                i = end + 1;
                setFormat(start, i - start, string);
            } else ++i;
        } else if (wordStart(text[i])) {
            while (i < text.size() && wordPart(text[i])) ++i;
            if (keywords.contains(text.mid(start, i - start).toUpper())) setFormat(start, i - start, keyword);
        } else if (text[i].isDigit() || (text[i] == '.' && i + 1 < text.size() && text[i + 1].isDigit())) {
            while (i < text.size() && text[i].isDigit()) ++i;
            if (i < text.size() && text[i] == '.') {
                ++i; while (i < text.size() && text[i].isDigit()) ++i;
            }
            if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
                int end = i + 1;
                if (end < text.size() && (text[end] == '+' || text[end] == '-')) ++end;
                const int digits = end;
                while (end < text.size() && text[end].isDigit()) ++end;
                if (end > digits) i = end;
            }
            setFormat(start, i - start, number);
        } else ++i;
    }
    const auto signature = QString::number(state->depth) + ':' + state->quote + ':'
        + QString::number(state->escapes) + ':' + state->dollar;
    if (!states.contains(signature)) states.insert(signature, states.size());
    setCurrentBlockState(states.value(signature));
    setCurrentBlockUserData(state);
}
