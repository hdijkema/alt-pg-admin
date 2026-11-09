#ifndef SAVEDQUERIESPANEL_H
#define SAVEDQUERIESPANEL_H
#include "querylibrary.h"
#include <QWidget>

class QLineEdit;
class QListWidget;
class QComboBox;
// goal: Find and manage named SQL files without owning editor or connection state.
// pre: GUI-thread use with a dedicated query-library directory.
// post: Opening/saving emits requests; successful rename/delete emits the exact names.
// internals: QueryLibrary owns file operations, QListWidget lists names only and
//            search hides nonmatching entries without loading their SQL contents.
class SavedQueriesPanel : public QWidget
{
    Q_OBJECT
public:
    explicit SavedQueriesPanel(const QString &directory, QWidget *parent = nullptr);
    QueryLibrary &library() { return store; }
    // Relist filenames and keep the requested/current selection when still present.
    void refresh(const QString &selected = QString());
    // Current category filter; all categories and uncategorised both yield an empty name for new saves.
    QString selectedCategory() const;
signals:
    void saveRequested();
    void openRequested(const QString &name);
    void queryRenamed(const QString &from, const QString &to);
    void queryRemoved(const QString &name);
private:
    void filter();
    void renameQuery();
    void removeQuery();
    void changeCategory();
    QString selectedName() const;
    QueryLibrary store;
    QLineEdit *search;
    QListWidget *list;
    QComboBox *category;
};
#endif
