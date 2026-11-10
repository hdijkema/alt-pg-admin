#ifndef QUERYTAB_H
#define QUERYTAB_H

#include <QWidget>
#include <QElapsedTimer>

class QPlainTextEdit;
class QTabWidget;
class QComboBox;
class QCheckBox;
class QLabel;
class QTableView;
class CellViewer;
class ResultModel;
class QPushButton;
class QToolButton;
class QTimer;
struct QueryResult;

// goal: Own one editor, result model and detail/output views for a selected connection.
// pre: GUI-thread use; PgAdmMain owns session dispatch and delivers ordered batches.
// post: Results are read-only; UI choices never silently change the target database.
// internals: ResultModel stores sections; QTableView presents them; CellViewer inspects
//            full values. File exports use immutable snapshots in QtConcurrent.
class QueryTab : public QWidget
{
    Q_OBJECT
public:
    explicit QueryTab(QWidget *parent = nullptr);
    QPlainTextEdit *editor() const { return sqlEditor; }
    QString filePath() const { return path; }
    void setFilePath(const QString &value) { path = value; }
    bool hasUtf8Bom() const { return utf8Bom; }
    void setUtf8Bom(bool value) { utf8Bom = value; }
    void showPlan();
    void setConnections(const QVariantList &connections);
    QString connectionId() const;
    void selectConnection(const QString &id);
    // Set the preference for the next execution, without rewriting existing results.
    void setAllResults(bool enabled);
    // Clear prior output, freeze execution options and preserve column widths.
    void startExecution();
    // Append one worker batch and maintain section spans and the latest EXPLAIN text.
    void appendResult(const QueryResult &result);
    void appendMessage(const QString &message);
    // Restore execution controls and show the session's final transaction status.
    void finishExecution(bool success, const QString &transaction);
    // Explicit execution-to-file action, distinct from exporting the retained preview.
    void setFullExportAvailable(bool enabled);
    void startFullExport(const QString &destination);
    void updateFullExport(qint64 rows);
    bool isRunning() const { return running; }
    // Return the selected SQL, normalising paragraph separators, or the whole editor.
    QString sqlToExecute() const;
    // Copy every stored section as TSV plus escaped HTML, preserving clipping markers.
    void copyAll();
    // Apply bounded widths by column position. Programmatic restoration emits no change.
    void setColumnWidths(const QVariantList &widths);
    QVariantList columnWidths() const;

signals:
    void allResultsChanged(bool enabled);
    void connectionChanged();
    void fullExportRequested(bool excel);
    void columnWidthsChanged(const QVariantList &widths);

private:
    QPlainTextEdit *sqlEditor;
    QTabWidget *output;
    QComboBox *connections;
    QCheckBox *allResults;
    QTableView *table;
    ResultModel *model;
    CellViewer *details;
    QPlainTextEdit *messages;
    QPlainTextEdit *plan;
    QLabel *state;
    bool running = false;
    bool showAll = true;
    QString path;
    bool utf8Bom = false;
    QVector<int> widths;
    void applyColumnWidths();
    void fitColumns();
    void exportResults(bool excel);
    void updateExportActions();
    QPushButton *copyButton;
    QPushButton *csvButton;
    QPushButton *xlsxButton;
    bool exporting = false;
    QToolButton *fullExportButton;
    QTimer *fullExportTimer;
    QElapsedTimer fullExportElapsed;
    qint64 fullExportRows = 0;
    bool canExecute = false;
};

#endif
