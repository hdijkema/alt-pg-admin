#include "database/pgsession.h"
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <QTimer>

class SessionTest : public QObject
{
    Q_OBJECT
private:
    QVariantMap profile(int port) const
    {
        return {{"host", "127.0.0.1"}, {"port", port}, {"database", "postgres"},
                {"user", "postgres"}, {"sslmode", "disable"}, {"timeout", 2}};
    }
    QVariantMap integrationProfile() const
    {
        auto result = profile(qEnvironmentVariableIntValue("PGADM_TEST_PORT"));
        return result;
    }
private slots:
    void refusedConnection()
    {
        QTcpServer reservation;
        QVERIFY(reservation.listen(QHostAddress::LocalHost));
        const int port = reservation.serverPort();
        reservation.close();
        PgSession session;
        QSignalSpy failed(&session, &PgSession::failed);
        session.open(profile(port));
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QCOMPARE(session.state(), PgSession::Disconnected);
        QVERIFY(!failed.at(0).at(0).toString().isEmpty());
        QVERIFY(!failed.at(0).at(1).toBool());
    }

    void timeoutKeepsEventLoopResponsive()
    {
        QTcpServer silentServer;
        QVERIFY(silentServer.listen(QHostAddress::LocalHost));
        PgSession session;
        QSignalSpy failed(&session, &PgSession::failed);
        int heartbeats = 0;
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, this, [&] { ++heartbeats; });
        heartbeat.start(20);
        session.open(profile(silentServer.serverPort()));
        QTRY_VERIFY_WITH_TIMEOUT(silentServer.hasPendingConnections(), 2000);
        auto *socket = silentServer.nextPendingConnection();
        QVERIFY(socket);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QVERIFY(heartbeats > 20);
        QVERIFY(failed.at(0).at(0).toString().contains("time-out"));
        QCOMPARE(session.state(), PgSession::Disconnected);
        delete socket;
    }

    void cancelConnection()
    {
        QTcpServer silentServer;
        QVERIFY(silentServer.listen(QHostAddress::LocalHost));
        PgSession session;
        QSignalSpy closed(&session, &PgSession::disconnected);
        QSignalSpy failed(&session, &PgSession::failed);
        session.open(profile(silentServer.serverPort()));
        QTRY_VERIFY_WITH_TIMEOUT(silentServer.hasPendingConnections(), 2000);
        session.close();
        QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 2000);
        QCOMPARE(session.state(), PgSession::Disconnected);
        QCOMPARE(failed.count(), 0);
    }

    void connectAndReconnect()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Set PGADM_TEST_PORT to a disposable local PostgreSQL cluster.");
        PgSession session;
        QSignalSpy connected(&session, &PgSession::connected);
        QSignalSpy closed(&session, &PgSession::disconnected);
        session.open(integrationProfile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QCOMPARE(session.state(), PgSession::Connected);
        QCOMPARE(session.information().value("database").toString(), QString("postgres"));
        QVERIFY(session.information().value("backendPid").toInt() > 0);
        QVERIFY(session.information().value("clientVersion").toInt() >= 180000);
        QVERIFY(session.information().value("serverVersion").toString().startsWith("18."));
        QVERIFY(!session.information().value("serverVersion").toString().isEmpty());
        session.close();
        QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, 2000);
        session.open(integrationProfile());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 2, 5000);
    }

    void passwordAuthentication()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires the disposable test cluster and test role.");
        auto connection = integrationProfile();
        connection["user"] = "pgadm_test_auth";
        PgSession session;
        QSignalSpy failed(&session, &PgSession::failed);
        QSignalSpy connected(&session, &PgSession::connected);
        session.open(connection);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QVERIFY(failed.at(0).at(1).toBool());
        session.open(connection, "deliberately-wrong");
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, 5000);
        QVERIFY(!failed.at(1).at(1).toBool());
        session.open(connection, "temporary-test-only");
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        QCOMPARE(session.information().value("user").toString(), QString("pgadm_test_auth"));
    }

    void missingDatabase()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires the disposable test cluster.");
        auto connection = integrationProfile();
        connection["database"] = "pgadm_missing_database";
        PgSession session;
        QSignalSpy failed(&session, &PgSession::failed);
        session.open(connection);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QCOMPARE(session.state(), PgSession::Disconnected);
        QVERIFY(failed.at(0).at(0).toString().contains("pgadm_missing_database"));
    }

    void hostnameConnection()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires the disposable test cluster.");
        auto connection = integrationProfile();
        connection["host"] = "localhost";
        connection["timeout"] = 10;
        PgSession session;
        QSignalSpy connected(&session, &PgSession::connected);
        QSignalSpy failed(&session, &PgSession::failed);
        session.open(connection);
        QTRY_VERIFY_WITH_TIMEOUT(connected.count() == 1 || failed.count() > 0, 12000);
        QVERIFY2(connected.count() == 1, failed.isEmpty() ? "No hostname connection result" : qPrintable(failed.at(0).at(0).toString()));
        QCOMPARE(session.state(), PgSession::Connected);
    }

    void sslRequired()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires the disposable test cluster without TLS.");
        auto connection = integrationProfile();
        connection["sslmode"] = "require";
        PgSession session;
        QSignalSpy failed(&session, &PgSession::failed);
        session.open(connection);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QCOMPARE(session.state(), PgSession::Disconnected);
        QVERIFY(failed.at(0).at(0).toString().contains("SSL"));
    }

    void serverDisconnect()
    {
        if (!qEnvironmentVariableIsSet("PGADM_TEST_PORT")) QSKIP("Requires the disposable test cluster.");
        // Authenticate through a proxy, then sever its TCP connection.
        QTcpServer proxy;
        QVERIFY(proxy.listen(QHostAddress::LocalHost));
        PgSession session;
        QSignalSpy connected(&session, &PgSession::connected);
        QSignalSpy failed(&session, &PgSession::failed);
        session.open(profile(proxy.serverPort()));
        QTRY_VERIFY_WITH_TIMEOUT(proxy.hasPendingConnections(), 2000);
        auto *client = proxy.nextPendingConnection();
        QTcpSocket upstream;
        upstream.connectToHost(QHostAddress::LocalHost, static_cast<quint16>(qEnvironmentVariableIntValue("PGADM_TEST_PORT")));
        connect(client, &QTcpSocket::readyRead, &upstream, [&] { upstream.write(client->readAll()); });
        connect(&upstream, &QTcpSocket::readyRead, client, [&] { client->write(upstream.readAll()); });
        if (client->bytesAvailable()) upstream.write(client->readAll());
        QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, 5000);
        client->abort();
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
        QCOMPARE(session.state(), PgSession::Disconnected);
        delete client;
    }
};

QTEST_GUILESS_MAIN(SessionTest)
#include "tst_pgsession.moc"
