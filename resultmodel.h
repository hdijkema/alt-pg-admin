#ifndef RESULTMODEL_H
#define RESULTMODEL_H
#include "queryresult.h"
#include <QAbstractTableModel>

// goal: Expose heterogeneous query results through one native table model.
// pre: Mutate only on the GUI thread. Batches for a statement arrive in order.
// post: Values keep their stored precision; DisplayRole uses short previews only.
// internals: sections owns QueryResults; rows indexes headings/columns/cells/status
//            into those sections. append() inserts model rows without per-cell widgets.
class ResultModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    explicit ResultModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
    // Remove every stored section and reset the Qt model to one empty column.
    void clear();
    // Append a batch, dropping previous sections when allResults is false and a
    // new statement begins. Return the first inserted flat row for heading spans.
    int append(const QueryResult &batch, bool allResults);
    // Borrow a stored value at a data row; nullptr for headings, status or empty cells.
    const ResultCell *cell(const QModelIndex &index) const;
    bool isHeading(int row) const;
    // Produce TSV overview text using stored values and explicit clipping markers.
    QString text() const;
    // Return only the last single-column QUERY PLAN result, preserving indentation.
    QString lastPlan() const;
    const QVector<QueryResult> &results() const { return sections; }

private:
    enum RowKind { Heading, Columns, Values, Status };
    struct Row { int section; RowKind kind; int valueRow = -1; };
    QVector<QueryResult> sections;
    QVector<Row> rows;
    int width = 1;
};
#endif
