#ifndef SQLEDITOR_H
#define SQLEDITOR_H
#include <QPlainTextEdit>
class QCompleter;
class QDialog;
class QLineEdit;
class QCheckBox;
class QLabel;

// goal: Provide native SQL editing, literal search/replace and explicit completion.
// pre: GUI-thread use; no database connection is needed.
// post: Editing keeps Qt undo history; completion never executes SQL.
// internals: QTextDocument searches feed a modeless dialog; QCompleter combines
//            shared SQL keywords with identifiers already present in the document.
class SqlEditor : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit SqlEditor(QWidget *parent = nullptr);
    void showSearch(bool replace = false);
    void findNext(bool backwards = false);
protected:
    void keyPressEvent(QKeyEvent *event) override;
private:
    void complete();
    void replaceCurrent();
    void replaceAll();
    QTextDocument::FindFlags searchFlags() const;
    QCompleter *completion;
    QDialog *searchDialog;
    QLineEdit *needle;
    QLineEdit *replacement;
    QCheckBox *matchCase;
    QCheckBox *wholeWord;
    QLabel *searchStatus;
    QWidget *replaceRow;
};
#endif
