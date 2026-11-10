#ifndef SQLHIGHLIGHTER_H
#define SQLHIGHLIGHTER_H
#include <QSyntaxHighlighter>
#include <QHash>
#include <QStringList>

// Return the shared SQL keyword vocabulary; no connection or document is needed.
QStringList sqlKeywords();

// goal: Colour editor SQL without changing its text or dirty state.
// pre: Attach to a GUI-thread QTextDocument; no PostgreSQL connection is required.
// post: Quotes/comments carry across blocks; quoted keywords are not highlighted as SQL.
// internals: highlightBlock() scans tokens and stores nested-comment/quote/dollar
//            state in SqlBlockState; stable state IDs trigger Qt's incremental rescans.
class SqlHighlighter : public QSyntaxHighlighter
{
public:
    explicit SqlHighlighter(QTextDocument *document);
protected:
    void highlightBlock(const QString &text) override;
private:
    QTextCharFormat keyword, string, comment, number, identifier;
    QHash<QString, int> states;
};
#endif
