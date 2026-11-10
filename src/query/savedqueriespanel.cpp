#include "savedqueriespanel.h"
#include <QGridLayout>
#include <QComboBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

// Build filename search and management controls; delegate actual editor operations through signals.
SavedQueriesPanel::SavedQueriesPanel(const QString &directory, QWidget *parent) : QWidget(parent), store(directory)
{
    setObjectName("savedQueriesPanel");
    auto *layout = new QVBoxLayout(this);
    search = new QLineEdit(this);
    search->setObjectName("savedQuerySearch");
    search->setPlaceholderText(tr("Zoek een query..."));
    layout->addWidget(search);
    category = new QComboBox(this);
    category->setObjectName("savedQueryCategory");
    category->setToolTip(tr("Toon alle queries of kies een categorie."));
    layout->addWidget(category);
    list = new QListWidget(this);
    list->setObjectName("savedQueryList");
    layout->addWidget(list, 1);
    auto *buttons = new QGridLayout;
    auto *save = new QPushButton(tr("Huidige opslaan..."), this);
    save->setObjectName("saveNamedQueryButton");
    save->setToolTip(tr("Bewaar de volledige query uit de huidige tab onder een naam."));
    auto *open = new QPushButton(tr("Openen"), this);
    auto *rename = new QPushButton(tr("Hernoemen..."), this);
    auto *remove = new QPushButton(tr("Verwijderen..."), this);
    auto *categorise = new QPushButton(tr("Categorie..."), this);
    categorise->setObjectName("categoriseSavedQueryButton");
    categorise->setToolTip(tr("Kies een bestaande categorie of typ een nieuwe naam."));
    rename->setObjectName("renameSavedQueryButton");
    remove->setObjectName("removeSavedQueryButton");
    buttons->addWidget(save, 0, 0);
    buttons->addWidget(open, 0, 1);
    buttons->addWidget(rename, 1, 0);
    buttons->addWidget(remove, 1, 1);
    buttons->addWidget(categorise, 2, 0, 1, 2);
    layout->addLayout(buttons);
    auto *hint = new QLabel(tr("Dubbelklik of druk Enter om een query te openen. Er wordt niets automatisch uitgevoerd."), this);
    hint->setWordWrap(true);
    layout->addWidget(hint);
    connect(save, &QPushButton::clicked, this, &SavedQueriesPanel::saveRequested);
    connect(open, &QPushButton::clicked, this, [this] { if (!selectedName().isEmpty()) emit openRequested(selectedName()); });
    connect(list, &QListWidget::itemActivated, this, [this] { if (!selectedName().isEmpty()) emit openRequested(selectedName()); });
    connect(rename, &QPushButton::clicked, this, &SavedQueriesPanel::renameQuery);
    connect(remove, &QPushButton::clicked, this, &SavedQueriesPanel::removeQuery);
    connect(categorise, &QPushButton::clicked, this, &SavedQueriesPanel::changeCategory);
    connect(search, &QLineEdit::textChanged, this, &SavedQueriesPanel::filter);
    connect(category, &QComboBox::currentIndexChanged, this, &SavedQueriesPanel::filter);
    auto updateButtons = [this, open, rename, remove, categorise] {
        const bool selected = !selectedName().isEmpty();
        open->setEnabled(selected); rename->setEnabled(selected); remove->setEnabled(selected);
        categorise->setEnabled(selected);
    };
    connect(list, &QListWidget::itemSelectionChanged, this, updateButtons);
    connect(search, &QLineEdit::textChanged, this, updateButtons);
    connect(category, &QComboBox::currentIndexChanged, this, updateButtons);
    refresh();
    updateButtons();
}

QString SavedQueriesPanel::selectedCategory() const { return category->currentData().toString(); }

QString SavedQueriesPanel::selectedName() const
{
    return list->currentItem() && !list->currentItem()->isHidden() ? list->currentItem()->text() : QString();
}

// Relist names and restore a surviving visible selection without loading SQL contents.
void SavedQueriesPanel::refresh(const QString &selected)
{
    const auto previous = selected.isEmpty() ? selectedName() : selected;
    auto previousCategory = category->currentData();
    if (!selected.isEmpty() && previousCategory.isValid()) previousCategory = store.category(selected);
    const QSignalBlocker block(category);
    category->clear();
    category->addItem(tr("Alle categorieën"));
    category->addItem(tr("Zonder categorie"), QString(""));
    for (const auto &value : store.categories()) category->addItem(value, value);
    category->setCurrentIndex(qMax(0, category->findData(previousCategory)));
    list->clear();
    list->addItems(store.names());
    for (int i = 0; i < list->count(); ++i) {
        const auto value = store.category(list->item(i)->text());
        list->item(i)->setData(Qt::UserRole, value);
        list->item(i)->setToolTip(value.isEmpty() ? tr("Zonder categorie") : value);
    }
    filter();
    for (int i = 0; i < list->count(); ++i)
        if (list->item(i)->text() == previous) list->setCurrentRow(i);
}

// Hide names that do not match the search text; hidden selections cannot be opened or deleted.
void SavedQueriesPanel::filter()
{
    for (int i = 0; i < list->count(); ++i) {
        const auto *item = list->item(i);
        const bool matchesCategory = !category->currentData().isValid() || item->data(Qt::UserRole) == category->currentData();
        list->item(i)->setHidden(!matchesCategory || !item->text().contains(search->text(), Qt::CaseInsensitive));
    }
}

// Assign an existing/new category; an empty value puts the query back among uncategorised scripts.
void SavedQueriesPanel::changeCategory()
{
    const auto name = selectedName();
    if (name.isEmpty()) return;
    QStringList choices{QString("")};
    choices.append(store.categories());
    bool ok = false;
    const auto chosen = QInputDialog::getItem(this, tr("Query categoriseren"),
        tr("Categorie (kies of typ; leeg = zonder categorie):"), choices,
        qMax(0, choices.indexOf(store.category(name))), true, &ok);
    if (!ok) return;
    QString error;
    if (!store.setCategory(name, chosen, &error)) { QMessageBox::warning(this, tr("Categorie opslaan mislukt"), error); return; }
    refresh(name);
}

// Ask for a replacement name and notify editors only after the file rename succeeds.
void SavedQueriesPanel::renameQuery()
{
    const auto from = selectedName();
    if (from.isEmpty()) return;
    bool ok = false;
    const auto to = QInputDialog::getText(this, tr("Query hernoemen"), tr("Nieuwe naam:"), QLineEdit::Normal, from, &ok).trimmed();
    if (!ok) return;
    QString error;
    if (!store.rename(from, to, &error)) { QMessageBox::warning(this, tr("Hernoemen mislukt"), error); return; }
    emit queryRenamed(from, to);
    refresh(to);
}

// Confirm removal and notify the main window so open editors can retain their text.
void SavedQueriesPanel::removeQuery()
{
    const auto name = selectedName();
    if (name.isEmpty()) return;
    if (QMessageBox::question(this, tr("Bewaarde query verwijderen"), tr("'%1' verwijderen? Open tabbladen blijven behouden.").arg(name)) != QMessageBox::Yes) return;
    QString error;
    if (!store.remove(name, &error)) { QMessageBox::warning(this, tr("Verwijderen mislukt"), error); return; }
    emit queryRemoved(name);
    refresh();
}
