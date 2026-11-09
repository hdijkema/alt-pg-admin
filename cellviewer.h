#ifndef CELLVIEWER_H
#define CELLVIEWER_H
#include "queryresult.h"
#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;

// goal: Inspect a stored cell without loading its entire value into the text widget.
// pre: GUI-thread use; ResultCell already reflects any query storage limits.
// post: Copy/save use the stored value, not a preview; clipping remains visible.
// internals: setValue() copies the cell; showPage() renders 50k text characters or
//            4 KiB binary hex at a time, with text wrapping and explicit navigation.
class CellViewer : public QWidget
{
    Q_OBJECT
public:
    explicit CellViewer(QWidget *parent = nullptr);
    // Select a cell and reset navigation to its first page.
    void setValue(const ResultCell &cell);
    // Release the previous value and reset the empty-selection UI.
    void clearValue();
private:
    void showPage();
    void saveValue();
    ResultCell value;
    int page = 0;
    QLabel *description;
    QLabel *pageLabel;
    QPlainTextEdit *contents;
    QPushButton *previous;
    QPushButton *next;
};
#endif
