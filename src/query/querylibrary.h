#ifndef QUERYLIBRARY_H
#define QUERYLIBRARY_H
#include <QStringList>

// goal: Store named scripts as ordinary UTF-8 files with optional categories.
// pre: Supply a dedicated local directory and a non-null error pointer to I/O methods.
// post: Writes are atomic; rejected names/duplicates leave existing scripts intact.
// internals: filePath() percent-encodes names, names() lists only filenames and
//            existingName() prevents case-insensitive duplicates. No SQL is executed.
//            categories.ini stores category metadata independently of script text/paths.
class QueryLibrary
{
public:
    explicit QueryLibrary(const QString &directory);
    // List decoded names, sorted without reading SQL contents.
    QStringList names() const;
    // Return the canonical stored spelling, or an empty string if not present.
    QString existingName(const QString &name) const;
    // Resolve an encoded filename inside the configured directory; performs no I/O.
    QString filePath(const QString &name) const;
    // Read category metadata; missing metadata means uncategorised. Only used categories are listed.
    QString category(const QString &name) const;
    QStringList categories() const;
    // Assign an existing query; empty category removes its assignment. SQL/path stay unchanged.
    bool setCategory(const QString &name, const QString &category, QString *error);
    // Read UTF-8, accepting a BOM. Empty SQL is valid; inspect error for failure.
    QString load(const QString &name, QString *error) const;
    // Save exactly sql; overwrite must explicitly permit an existing name.
    bool save(const QString &name, const QString &sql, bool overwrite, QString *error);
    // Rename without overwriting another script. Report any filesystem failure in error.
    bool rename(const QString &from, const QString &to, QString *error);
    // Remove only the named script; callers retain responsibility for open editors.
    bool remove(const QString &name, QString *error);
private:
    bool validName(const QString &name, QString *error) const;
    QString directory;
};
#endif
