#include <QtTest/QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>

#include "plugin/NodePlugin.h"

// ── Helper: parse JSON string ─────────────────────────────────────────────────
static QJsonObject parseObj(const QString& s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

// ── FakeNodeServer — minimal HTTP server returning /cryptarchia/info ──────────
class FakeNodeServer : public QObject
{
    Q_OBJECT
public:
    explicit FakeNodeServer(QObject* parent = nullptr) : QObject(parent)
    {
        m_server = new QTcpServer(this);
        connect(m_server, &QTcpServer::newConnection, this, &FakeNodeServer::handleConnection);
    }

    bool listen(quint16 port = 0)
    {
        bool ok = m_server->listen(QHostAddress::LocalHost, port);
        if (ok) m_port = m_server->serverPort();
        return ok;
    }

    quint16 port() const { return m_port; }

    // Set the response body returned for /cryptarchia/info
    void setInfoResponse(const QByteArray& json) { m_infoBody = json; }

private slots:
    void handleConnection()
    {
        QTcpSocket* sock = m_server->nextPendingConnection();
        connect(sock, &QTcpSocket::readyRead, this, [this, sock]() {
            sock->readAll();  // discard request
            QByteArray body   = m_infoBody;
            QByteArray resp   = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                "Content-Length: " + QByteArray::number(body.size()) +
                                "\r\nConnection: close\r\n\r\n" + body;
            sock->write(resp);
            sock->flush();
            sock->disconnectFromHost();
        });
    }

private:
    QTcpServer* m_server = nullptr;
    QByteArray  m_infoBody;
    quint16     m_port = 0;
};

// ── Test class ────────────────────────────────────────────────────────────────
class TestNodePlugin : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        // Clean QSettings state before each test
        QSettings s;
        s.remove(QStringLiteral("logos-node"));
    }

    // ── Config tests ──────────────────────────────────────────────────────────
    void testSetNodeConfigAllPaths()
    {
        NodePlugin p;
        auto r = parseObj(p.setNodeConfig("/bin/node", "/circuits", "/cfg.yaml", "/data"));
        QVERIFY(!r.contains("error"));
        QCOMPARE(r["ok"].toBool(), true);
    }

    void testSetNodeConfigMissingBinary()
    {
        NodePlugin p;
        auto r = parseObj(p.setNodeConfig("", "/circuits", "/cfg.yaml", "/data"));
        QVERIFY(r.contains("error"));
    }

    void testSetNodeConfigMissingCircuits()
    {
        NodePlugin p;
        auto r = parseObj(p.setNodeConfig("/bin/node", "", "/cfg.yaml", "/data"));
        QVERIFY(r.contains("error"));
    }

    void testGetNodeConfigRoundTrip()
    {
        NodePlugin p;
        p.setNodeConfig("/bin/node", "/circuits", "/cfg.yaml", "/data");
        auto r = parseObj(p.getNodeConfig());
        QCOMPARE(r["binaryPath"].toString(),   QString("/bin/node"));
        QCOMPARE(r["circuitsPath"].toString(), QString("/circuits"));
        QCOMPARE(r["configPath"].toString(),   QString("/cfg.yaml"));
        QCOMPARE(r["dataDir"].toString(),      QString("/data"));
        QCOMPARE(r["configured"].toBool(),     true);
    }

    void testGetNodeConfigConfiguredFalseWhenEmpty()
    {
        NodePlugin p;
        auto r = parseObj(p.getNodeConfig());
        QCOMPARE(r["configured"].toBool(), false);
    }

    void testGetNodeUrlDefault()
    {
        NodePlugin p;
        QCOMPARE(p.getNodeUrl(), QString("http://127.0.0.1:8080"));
    }

    // ── Status tests — FakeNodeServer ─────────────────────────────────────────
    void testGetStatusOnline()
    {
        FakeNodeServer srv;
        QVERIFY(srv.listen());
        srv.setInfoResponse(R"({"lib":"aa","lib_slot":500,"tip":"bb","slot":1000,"height":42,"mode":"Online"})");

        // Point the plugin at our fake server port via QSettings
        QSettings s;
        s.setValue("logos-node/httpPort", srv.port());
        s.sync();

        NodePlugin p;
        auto r = parseObj(p.getStatus());
        QCOMPARE(r["running"].toBool(),       true);
        QCOMPARE(r["mode"].toString(),        QString("Online"));
        QCOMPARE(r["slot"].toInt(),           1000);
        QCOMPARE(r["libSlot"].toInt(),        500);
        QCOMPARE(r["height"].toInt(),         42);
    }

    void testGetStatusOfflineWhenNoServer()
    {
        // No fake server — any port that isn't listening
        QSettings s;
        s.setValue("logos-node/httpPort", 19999);
        s.sync();

        NodePlugin p;
        auto r = parseObj(p.getStatus());
        QCOMPARE(r["running"].toBool(), false);
    }

    void testGetStatusSyncing()
    {
        FakeNodeServer srv;
        QVERIFY(srv.listen());
        srv.setInfoResponse(R"({"lib":"aa","lib_slot":250,"tip":"bb","slot":1000,"height":13,"mode":"Syncing"})");

        QSettings s;
        s.setValue("logos-node/httpPort", srv.port());
        s.sync();

        NodePlugin p;
        auto r = parseObj(p.getStatus());
        QCOMPARE(r["running"].toBool(),    true);
        QCOMPARE(r["mode"].toString(),     QString("Syncing"));
        QCOMPARE(r["libSlot"].toInt(),     250);
    }

    // ── Log tests ─────────────────────────────────────────────────────────────
    void testGetLogEmptyOnStart()
    {
        NodePlugin p;
        QJsonDocument doc = QJsonDocument::fromJson(p.getLog().toUtf8());
        QVERIFY(doc.isArray());
        QCOMPARE(doc.array().size(), 0);
    }
};

QTEST_MAIN(TestNodePlugin)
#include "test_node_plugin.moc"
