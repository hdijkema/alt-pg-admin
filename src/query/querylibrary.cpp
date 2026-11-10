#include "querylibrary.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QSettings>
#include <QUrl>

QueryLibrary::QueryLibrary(const QString &directory) : directory(QDir(directory).absolutePath()) {}

// Keep metadata beside the scripts; a library without this file remains fully readable.
QString QueryLibrary::category(const QString &name) const
{
    QSettings metadata(QDir(directory).filePath("categories.ini"), QSettings::IniFormat);
    return metadata.value("categories/" + QString::fromLatin1(QUrl::toPercentEncoding(name))).toString();
}

// Ignore stale metadata for removed files and present a case-insensitive category list.
QStringList QueryLibrary::categories() const
{
    QStringList result;
    for (const auto &name : names()) {
        const auto value = category(name);
        if (!value.isEmpty() && !result.contains(value, Qt::CaseInsensitive)) result.append(value);
    }
    result.sort(Qt::CaseInsensitive);
    return result;
}

// Persist category assignment atomically through QSettings, without touching the SQL file.
bool QueryLibrary::setCategory(const QString &name, const QString &categoryName, QString *error)
{
    error->clear();
    const auto existing = existingName(name);
    if (existing.isEmpty()) { *error = QCoreApplication::translate("querylibrary", "De bewaarde query bestaat niet meer."); return false; }
    auto value = categoryName.trimmed();
    if (value.size() > 100) { *error = QCoreApplication::translate("querylibrary", "Gebruik een categorienaam van maximaal 100 tekens."); return false; }
    for (const auto character : value)
        if (character.category() == QChar::Other_Control) { *error = QCoreApplication::translate("querylibrary", "De categorienaam bevat een stuurteken."); return false; }
    for (const auto &stored : categories())
        if (stored.compare(value, Qt::CaseInsensitive) == 0) { value = stored; break; }
    QSettings metadata(QDir(directory).filePath("categories.ini"), QSettings::IniFormat);
    if (metadata.status() != QSettings::NoError) { *error = QCoreApplication::translate("querylibrary", "De categorie-instellingen kunnen niet worden gelezen."); return false; }
    const auto key = "categories/" + QString::fromLatin1(QUrl::toPercentEncoding(existing));
    if (value.isEmpty()) metadata.remove(key);
    else metadata.setValue(key, value);
    metadata.sync();
    if (metadata.status() != QSettings::NoError) { *error = QCoreApplication::translate("querylibrary", "De categorie kan niet worden opgeslagen."); return false; }
    return true;
}

// Keep every query name inside the library directory using percent-encoded filenames.
QString QueryLibrary::filePath(const QString &name) const
{
    return QDir(directory).filePath("query_" + QString::fromLatin1(QUrl::toPercentEncoding(name)) + ".sql");
}

// List only encoded SQL filenames; opening the panel never reads the scripts themselves.
QStringList QueryLibrary::names() const
{
    QStringList result;
    for (const auto &file : QDir(directory).entryList({"query_*.sql"}, QDir::Files, QDir::Name))
        result.append(QUrl::fromPercentEncoding(file.mid(6, file.size() - 10).toLatin1()));
    result.sort(Qt::CaseInsensitive);
    return result;
}

// Resolve a trimmed name case-insensitively to avoid duplicates on Windows.
QString QueryLibrary::existingName(const QString &name) const
{
    for (const auto &existing : names())
        if (existing.compare(name.trimmed(), Qt::CaseInsensitive) == 0) return existing;
    return {};
}

// Reject empty, oversized or control-character names before touching the filesystem.
bool QueryLibrary::validName(const QString &name, QString *error) const
{
    error->clear();
    if (name.trimmed().isEmpty()) *error = QCoreApplication::translate("querylibrary", "Geef de query een naam.");
    else if (QUrl::toPercentEncoding(name).size() > 200) *error = QCoreApplication::translate("querylibrary", "De querynaam is te lang. Gebruik een kortere naam.");
    else for (const auto c : name) if (c.category() == QChar::Other_Control) { *error = QCoreApplication::translate("querylibrary", "De naam bevat een ongeldig stuurteken."); break; }
    return error->isEmpty();
}

// Read UTF-8 with optional BOM and distinguish valid empty SQL from a filesystem error.
QString QueryLibrary::load(const QString &name, QString *error) const
{
    error->clear();
    if (!validName(name, error)) return {};
    QFile file(filePath(name));
    if (!file.open(QIODevice::ReadOnly)) { *error = file.errorString(); return {}; }
    auto bytes = file.readAll();
    if (file.error() != QFileDevice::NoError) { *error = file.errorString(); return {}; }
    if (bytes.startsWith("\xEF\xBB\xBF")) bytes.remove(0, 3);
    return QString::fromUtf8(bytes);
}

// Check collisions, create the library directory and replace a script only after QSaveFile commits.
bool QueryLibrary::save(const QString &name, const QString &sql, bool overwrite, QString *error)
{
    if (!validName(name, error)) return false;
    const auto existing = existingName(name);
    if (!existing.isEmpty() && !overwrite) { *error = QCoreApplication::translate("querylibrary", "Er bestaat al een query met deze naam."); return false; }
    if (!QDir().mkpath(directory)) { *error = QCoreApplication::translate("querylibrary", "De map voor bewaarde queries kan niet worden gemaakt."); return false; }
    QSaveFile file(filePath(existing.isEmpty() ? name : existing));
    const auto bytes = sql.toUtf8();
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        *error = file.errorString(); return false;
    }
    return true;
}

// Rename one stored script while preventing replacement of another entry.
bool QueryLibrary::rename(const QString &from, const QString &to, QString *error)
{
    if (!validName(to, error) || !validName(from, error)) return false;
    if (from == to) return true;
    if (!existingName(to).isEmpty()) { *error = QCoreApplication::translate("querylibrary", "Er bestaat al een query met deze naam."); return false; }
    QFile file(filePath(from));
    const auto previousCategory = category(from);
    if (!file.rename(filePath(to))) { *error = file.errorString(); return false; }
    if (!setCategory(to, previousCategory, error)) {
        if (!file.rename(filePath(from))) *error += QCoreApplication::translate("querylibrary", " De query staat nu onder de nieuwe naam: ") + to;
        return false;
    }
    QSettings metadata(QDir(directory).filePath("categories.ini"), QSettings::IniFormat);
    metadata.remove("categories/" + QString::fromLatin1(QUrl::toPercentEncoding(from)));
    metadata.sync();
    return true;
}

// Delete one validated library file; editor dirty-state handling belongs to PgAdmMain.
bool QueryLibrary::remove(const QString &name, QString *error)
{
    if (!validName(name, error)) return false;
    QFile file(filePath(name));
    if (!file.remove()) { *error = file.errorString(); return false; }
    QSettings metadata(QDir(directory).filePath("categories.ini"), QSettings::IniFormat);
    metadata.remove("categories/" + QString::fromLatin1(QUrl::toPercentEncoding(name)));
    metadata.sync();
    return true;
}
