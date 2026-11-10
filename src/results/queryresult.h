#ifndef QUERYRESULT_H
#define QUERYRESULT_H

#include <QByteArray>
#include <QMetaType>
#include <QStringList>
#include <QVector>

// Stored cell payload. NULL, text and bytea remain distinct until a text format is
// requested. truncated means the worker retained only part of the original value;
// preview() must never be used to reconstruct an export or saved cell value.
struct ResultCell
{
    QString text;
    QByteArray binary;
    bool isNull = false;
    bool isBinary = false;
    bool truncated = false;
    qint64 sourceBytes = 0;
    QString fullText() const;
    QString preview() const;
};

// One statement's batch or completed section. Batches share number/title/columns;
// finished marks the terminal batch, and truncated remains set for omitted rows/cells.
// ResultModel merges ordered batches before presenting or exporting the section.
struct QueryResult
{
    int number = 0;
    QString title;
    QStringList columns;
    QVector<QVector<ResultCell>> rows;
    QString status;
    QString sqlState;
    bool finished = false;
    bool error = false;
    bool truncated = false;
    qint64 elapsedMs = 0;
};

Q_DECLARE_METATYPE(QueryResult)

#endif
