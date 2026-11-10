#include "sqleditor.h"
#include "sqlhighlighter.h"
#include <QAbstractItemView>
#include <QCheckBox>
#include <QCompleter>
#include <QDialog>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QShortcut>
#include <QStringListModel>
#include <QVBoxLayout>

// Own the search dialog and completer so each query tab retains its own search state.
SqlEditor::SqlEditor(QWidget *parent) : QPlainTextEdit(parent)
{
    new SqlHighlighter(document());
    completion = new QCompleter(this);
    completion->setWidget(this);
    completion->setCaseSensitivity(Qt::CaseInsensitive);
    completion->setModel(new QStringListModel(completion));
    connect(completion, qOverload<const QString &>(&QCompleter::activated), this, [this](const QString &word) {
        auto cursor = textCursor();
        cursor.select(QTextCursor::WordUnderCursor);
        cursor.insertText(word);
        setTextCursor(cursor);
    });
    searchDialog = new QDialog(this, Qt::Tool);
    searchDialog->setWindowTitle(tr("Zoeken en vervangen"));
    auto *layout = new QVBoxLayout(searchDialog);
    needle = new QLineEdit(searchDialog);
    needle->setObjectName("findText");
    needle->setPlaceholderText(tr("Zoeken naar"));
    layout->addWidget(needle);
    auto *options = new QHBoxLayout;
    matchCase = new QCheckBox(tr("Hoofdlettergevoelig"), searchDialog);
    wholeWord = new QCheckBox(tr("Hele woorden"), searchDialog);
    options->addWidget(matchCase);
    options->addWidget(wholeWord);
    layout->addLayout(options);
    auto *navigation = new QHBoxLayout;
    auto *previous = new QPushButton(tr("Vorige"), searchDialog);
    auto *next = new QPushButton(tr("Volgende"), searchDialog);
    navigation->addWidget(previous);
    navigation->addWidget(next);
    layout->addLayout(navigation);
    connect(previous, &QPushButton::clicked, this, [this] { findNext(true); });
    connect(next, &QPushButton::clicked, this, [this] { findNext(); });
    connect(needle, &QLineEdit::returnPressed, this, [this] { findNext(); });
    replaceRow = new QWidget(searchDialog);
    auto *replaceLayout = new QVBoxLayout(replaceRow);
    replaceLayout->setContentsMargins(0, 0, 0, 0);
    replacement = new QLineEdit(replaceRow);
    replacement->setObjectName("replaceText");
    replacement->setPlaceholderText(tr("Vervangen door"));
    replaceLayout->addWidget(replacement);
    auto *buttons = new QHBoxLayout;
    auto *one = new QPushButton(tr("Vervangen"), replaceRow);
    one->setObjectName("replaceOne");
    auto *all = new QPushButton(tr("Alles vervangen"), replaceRow);
    all->setObjectName("replaceAll");
    buttons->addWidget(one);
    buttons->addWidget(all);
    replaceLayout->addLayout(buttons);
    connect(one, &QPushButton::clicked, this, &SqlEditor::replaceCurrent);
    connect(all, &QPushButton::clicked, this, &SqlEditor::replaceAll);
    layout->addWidget(replaceRow);
    searchStatus = new QLabel(searchDialog);
    layout->addWidget(searchStatus);
    auto *close = new QPushButton(tr("Sluiten"), searchDialog);
    connect(close, &QPushButton::clicked, searchDialog, &QDialog::hide);
    layout->addWidget(close);
    for (auto *button : searchDialog->findChildren<QPushButton *>()) button->setAutoDefault(false);
    auto shortcut = [this](const QKeySequence &key, auto action) {
        auto *item = new QShortcut(key, this);
        item->setContext(Qt::WidgetWithChildrenShortcut);
        connect(item, &QShortcut::activated, this, action);
    };
    shortcut(QKeySequence::Find, [this] { showSearch(); });
    shortcut(QKeySequence("Ctrl+H"), [this] { showSearch(true); });
    shortcut(QKeySequence(Qt::Key_F3), [this] { findNext(); });
    shortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), [this] { findNext(true); });
}

// Reuse a single nonmodal dialog and seed searches from the current single-line selection.
void SqlEditor::showSearch(bool replace)
{
    const auto selected = textCursor().selectedText();
    if (!selected.isEmpty() && !selected.contains(QChar::ParagraphSeparator)) needle->setText(selected);
    replaceRow->setVisible(replace);
    searchStatus->clear();
    searchDialog->show();
    searchDialog->raise();
    needle->setFocus();
    needle->selectAll();
}

QTextDocument::FindFlags SqlEditor::searchFlags() const
{
    QTextDocument::FindFlags flags;
    if (matchCase->isChecked()) flags |= QTextDocument::FindCaseSensitively;
    if (wholeWord->isChecked()) flags |= QTextDocument::FindWholeWords;
    return flags;
}

// Search literally and wrap once, leaving the cursor unchanged when nothing matches.
void SqlEditor::findNext(bool backwards)
{
    if (needle->text().isEmpty()) { showSearch(); return; }
    auto flags = searchFlags();
    if (backwards) flags |= QTextDocument::FindBackward;
    auto found = document()->find(needle->text(), textCursor(), flags);
    const bool wrapped = found.isNull();
    if (wrapped) found = document()->find(needle->text(), backwards ? document()->characterCount() - 1 : 0, flags);
    if (!found.isNull()) setTextCursor(found);
    searchStatus->setText(found.isNull() ? tr("Niet gevonden") : wrapped ? tr("Zoeken hervat aan andere kant") : QString());
}

// Replace only a selection that satisfies the actual search flags, then advance.
void SqlEditor::replaceCurrent()
{
    if (needle->text().isEmpty()) return;
    auto cursor = textCursor();
    const auto found = document()->find(needle->text(), cursor.selectionStart(), searchFlags());
    if (!found.isNull() && found.selectionStart() == cursor.selectionStart() && found.selectionEnd() == cursor.selectionEnd())
        cursor.insertText(replacement->text());
    setTextCursor(cursor);
    findNext();
}

// Advance past replacements so inserted matches are not reprocessed; undo the batch in one step.
void SqlEditor::replaceAll()
{
    if (needle->text().isEmpty()) return;
    auto edit = textCursor();
    edit.beginEditBlock();
    int position = 0;
    int count = 0;
    while (true) {
        auto found = document()->find(needle->text(), position, searchFlags());
        if (found.isNull()) break;
        found.insertText(replacement->text());
        position = found.position();
        ++count;
    }
    edit.endEditBlock();
    searchStatus->setText(tr("%1 vervangingen").arg(count));
}

// Offer explicit keyword/local-identifier completion without server requests or background parsing.
void SqlEditor::complete()
{
    auto cursor = textCursor();
    cursor.select(QTextCursor::WordUnderCursor);
    QStringList words = sqlKeywords();
    const QString source = toPlainText().left(1024 * 1024);
    auto matches = QRegularExpression("\\b[A-Za-z_][A-Za-z_0-9$]*\\b").globalMatch(source);
    while (matches.hasNext()) words.append(matches.next().captured());
    for (int i = words.size() - 1; i >= 0; --i)
        if (words[i].compare(cursor.selectedText(), Qt::CaseInsensitive) == 0) words.removeAt(i);
    words.removeDuplicates();
    words.sort(Qt::CaseInsensitive);
    static_cast<QStringListModel *>(completion->model())->setStringList(words);
    completion->setCompletionPrefix(cursor.selectedText());
    if (!completion->completionCount()) return;
    completion->popup()->setCurrentIndex(completion->completionModel()->index(0, 0));
    auto rect = cursorRect();
    rect.setWidth(320);
    completion->complete(rect);
}

// Keep completion opt-in and let its popup own navigation/accept/cancel keys.
void SqlEditor::keyPressEvent(QKeyEvent *event)
{
    if (completion->popup()->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Enter: case Qt::Key_Return: case Qt::Key_Escape: case Qt::Key_Tab: case Qt::Key_Backtab:
            event->ignore();
            return;
        default: completion->popup()->hide();
        }
    }
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::ControlModifier) { complete(); return; }
    QPlainTextEdit::keyPressEvent(event);
}
