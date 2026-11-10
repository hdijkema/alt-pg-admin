#ifndef PASSWORDSTORE_H
#define PASSWORDSTORE_H
#include <QObject>
#include <QVariantMap>
#include <functional>

// goal: Keep optional connection passwords in the OS vault through QtKeychain.
// pre: GUI-thread use; callbacks must remain valid while this QObject exists.
// post: No plaintext fallback or password is persisted in application settings.
// internals: Each operation owns one asynchronous QtKeychain job. read() reports
//            a missing entry separately from a vault error; remove() is idempotent.
class PasswordStore : public QObject
{
    Q_OBJECT
public:
    explicit PasswordStore(QObject *parent = nullptr, const QString &service = "PgAdmQt");
    // Hash server ID, host, port and user; exclude database so server logins can be reused.
    static QString key(const QVariantMap &profile);
    // Callback arguments: found, password (only when found), error (empty on success/missing).
    void read(const QString &key, std::function<void(bool, const QString &, const QString &)> finished);
    // Write a vault entry; callback receives an empty error on success.
    void write(const QString &key, const QString &password, std::function<void(const QString &)> finished);
    // Delete a vault entry; absence also counts as success.
    void remove(const QString &key, std::function<void(const QString &)> finished);
private:
    QString service;
};
#endif
