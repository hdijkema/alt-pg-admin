#include "cellviewer.h"
#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>
#include <QTextOption>

// Build read-only paged text/hex navigation and actions that use the stored cell, not its preview.
CellViewer::CellViewer(QWidget *parent) : QWidget(parent)
{
    auto *layout = new QVBoxLayout(this);
    description = new QLabel(tr("Selecteer een resultaatcel voor details."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    contents = new QPlainTextEdit(this);
    contents->setReadOnly(true);
    contents->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    contents->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    contents->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    contents->setObjectName("cellContents");
    layout->addWidget(contents, 1);
    auto *navigation = new QHBoxLayout;
    previous = new QPushButton(tr("Vorige"), this);
    next = new QPushButton(tr("Volgende"), this);
    pageLabel = new QLabel(this);
    navigation->addWidget(previous);
    navigation->addWidget(pageLabel, 1);
    navigation->addWidget(next);
    layout->addLayout(navigation);
    auto *actions = new QHBoxLayout;
    auto *copy = new QPushButton(tr("Waarde kopiëren"), this);
    auto *save = new QPushButton(tr("Bewaren..."), this);
    actions->addWidget(copy);
    actions->addWidget(save);
    layout->addLayout(actions);
    connect(previous, &QPushButton::clicked, this, [this] { --page; showPage(); });
    connect(next, &QPushButton::clicked, this, [this] { ++page; showPage(); });
    connect(copy, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(value.fullText()); });
    connect(save, &QPushButton::clicked, this, &CellViewer::saveValue);
    previous->setEnabled(false);
    next->setEnabled(false);
}

// Copy the selected cell, describe its storage state and restart paging at the beginning.
void CellViewer::setValue(const ResultCell &cell)
{
    value = cell;
    page = 0;
    description->setText(value.isNull ? tr("NULL — geen waarde") : tr("%1 — %2 bytes%3")
        .arg(value.isBinary ? tr("Binair (hexweergave)") : tr("Tekst"))
        .arg(value.sourceBytes).arg(value.truncated ? tr(" — INGEKORT: alleen het opgeslagen deel is beschikbaar") : QString()));
    showPage();
}

// Release the previous cell and disable navigation until a new value is selected.
void CellViewer::clearValue()
{
    value = ResultCell();
    page = 0;
    contents->clear();
    pageLabel->clear();
    previous->setEnabled(false);
    next->setEnabled(false);
    description->setText(tr("Selecteer een resultaatcel voor details."));
}

// Bound the page index and render 50k text characters or 4 KiB of aligned binary hex.
void CellViewer::showPage()
{
    contents->setLineWrapMode(value.isBinary ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);
    const int size = value.isBinary ? value.binary.size() : value.text.size();
    const int pageSize = value.isBinary ? 4096 : 50000;
    const int pages = qMax(1, (size + pageSize - 1) / pageSize);
    page = qBound(0, page, pages - 1);
    const int offset = page * pageSize;
    if (value.isNull) contents->setPlainText("NULL");
    else if (value.isBinary) {
        QString text;
        const auto bytes = value.binary.mid(offset, pageSize);
        for (int i = 0; i < bytes.size(); i += 16) {
            const auto line = bytes.mid(i, 16);
            text += QString("%1  %2\n").arg(offset + i, 8, 16, QChar('0')).arg(QString::fromLatin1(line.toHex(' ')));
        }
        contents->setPlainText(text);
    } else contents->setPlainText(value.text.mid(offset, pageSize));
    previous->setEnabled(page > 0);
    next->setEnabled(page + 1 < pages);
    pageLabel->setText(tr("%1 / %2").arg(page + 1).arg(pages));
}

// Confirm clipped values and atomically save stored text or original bytea bytes.
void CellViewer::saveValue()
{
    if (value.truncated && QMessageBox::question(this, tr("Waarde ingekort"),
        tr("De volledige waarde is niet opgeslagen. Alleen het beschikbare deel bewaren?")) != QMessageBox::Yes) return;
    const auto path = QFileDialog::getSaveFileName(this, tr("Celwaarde bewaren"), QString(),
        value.isBinary ? tr("Binaire bestanden (*.bin);;Alle bestanden (*)") : tr("Tekstbestanden (*.txt);;Alle bestanden (*)"));
    if (path.isEmpty()) return;
    QSaveFile file(path);
    const auto bytes = value.isBinary ? value.binary : value.fullText().toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        QMessageBox::warning(this, tr("Bewaren mislukt"), file.errorString());
}
