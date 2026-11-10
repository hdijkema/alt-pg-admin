#include "passwordstore.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <qtkeychain/keychain.h>

PasswordStore::PasswordStore(QObject *parent, const QString &service) : QObject(parent), service(service) {}

// Hash the server login identity while deliberately sharing credentials across its databases.
QString PasswordStore::key(const QVariantMap &profile)
{
    const QJsonArray identity{profile.value("serverId", profile.value("id")).toString(),
        profile.value("host").toString(), profile.value("port", 5432).toInt(), profile.value("user").toString()};
    return "connection/" + QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(identity).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256).toHex());
}

// Run a QtKeychain read job with no insecure fallback and report absence separately from failure.
void PasswordStore::read(const QString &key, std::function<void(bool, const QString &, const QString &)> finished)
{
    auto *job = new QKeychain::ReadPasswordJob(service, this);
    job->setKey(key);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [job, finished] {
        const bool found = job->error() == QKeychain::NoError;
        const auto error = found || job->error() == QKeychain::EntryNotFound ? QString() : job->errorString();
        finished(found, found ? job->textData() : QString(), error);
    });
    job->start();
}

// Submit a vault-only password job and report its completion without exposing the secret.
void PasswordStore::write(const QString &key, const QString &password, std::function<void(const QString &)> finished)
{
    auto *job = new QKeychain::WritePasswordJob(service, this);
    job->setKey(key);
    job->setTextData(password);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [job, finished] { finished(job->error() ? job->errorString() : QString()); });
    job->start();
}

// Delete the vault entry asynchronously, treating an already absent entry as success.
void PasswordStore::remove(const QString &key, std::function<void(const QString &)> finished)
{
    auto *job = new QKeychain::DeletePasswordJob(service, this);
    job->setKey(key);
    job->setInsecureFallback(false);
    connect(job, &QKeychain::Job::finished, this, [job, finished] {
        finished(job->error() && job->error() != QKeychain::EntryNotFound ? job->errorString() : QString());
    });
    job->start();
}
