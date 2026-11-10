#ifndef RESULTEXPORTER_H
#define RESULTEXPORTER_H
#include "queryresult.h"

namespace ResultExporter {
// goal: Export stored result values, never the abbreviated table previews.
// pre: Pass an immutable result snapshot and an explicit output path. May run on a worker.
// post: A successful export atomically replaces the target. Failure preserves it.
// result: Empty QString on success, otherwise an actionable error description.
// internals: The private visit() traverses QueryResult sections. csv() writes quoted
//            UTF-8; xlsx() passes rows to QtXlsxWriter, which handles libxlsxwriter
//            and atomically publishes the result. No query is rerun by export.
QString csv(const QString &path, const QVector<QueryResult> &results);
QString xlsx(const QString &path, const QVector<QueryResult> &results, const QVector<int> &widths);
// Return an escaped table for clipboard pasting. Includes headings/status and
// explicit truncation markers; creates no file and executes no embedded text.
QString html(const QVector<QueryResult> &results);
// Report section- or cell-level clipping so the UI can confirm incomplete exports.
bool isTruncated(const QVector<QueryResult> &results);
}
#endif
