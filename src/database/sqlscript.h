#ifndef SQLSCRIPT_H
#define SQLSCRIPT_H
#include <QString>
#include <QVector>

struct SqlStatement { QString sql; QString name; QString keyword; };
class SqlScript
{
public:
    // goal: Split editor SQL into ordered statements with optional @resultaat names.
    // pre: error is non-null; standard_conforming_strings is on. No psql/COPY input.
    // post: Quoted/comment semicolons do not split SQL; keyword is the uppercase first token.
    // result: Statements, or an empty vector and error for an unterminated construct.
    // internals: One scan skips nested comments and quoted bodies before recognising
    //            statement boundaries; this is not a full PostgreSQL grammar parser.
    static QVector<SqlStatement> split(const QString &sql, QString *error);
};
#endif
