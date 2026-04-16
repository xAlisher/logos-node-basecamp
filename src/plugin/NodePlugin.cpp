#include "NodePlugin.h"

#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDateTime>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QProcessEnvironment>

// ── QSettings key prefix ─────────────────────────────────────────────────────
static constexpr const char* kBinaryPathKey  = "logos-node/binaryPath";
static constexpr const char* kCircuitsPathKey = "logos-node/circuitsPath";
static constexpr const char* kConfigPathKey  = "logos-node/configPath";
static constexpr const char* kDataDirKey     = "logos-node/dataDir";
static constexpr const char* kHttpPortKey    = "logos-node/httpPort";

// ── Helpers ──────────────────────────────────────────────────────────────────
QString NodePlugin::errorJson(const QString& msg)
{
    QJsonObject o;
    o[QStringLiteral("error")] = msg;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString NodePlugin::okJson()
{
    QJsonObject o;
    o[QStringLiteral("ok")] = true;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

void NodePlugin::appendLog(const QString& line, const QString& level)
{
    if (m_logBuffer.size() >= kMaxLogLines)
        m_logBuffer.removeFirst();
    LogEntry e;
    e.ts    = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    e.msg   = line.trimmed();
    e.level = level;
    m_logBuffer.append(e);
}

QString NodePlugin::formatLog() const
{
    QJsonArray arr;
    for (const auto& e : m_logBuffer) {
        QJsonObject o;
        o[QStringLiteral("ts")]    = e.ts;
        o[QStringLiteral("msg")]   = e.msg;
        o[QStringLiteral("level")] = e.level;
        arr.append(o);
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

bool NodePlugin::isNodeResponding() const
{
    QSettings s;
    int port = s.value(QLatin1String(kHttpPortKey), 8080).toInt();
    QUrl url(QStringLiteral("http://127.0.0.1:%1/cryptarchia/info").arg(port));

    QNetworkRequest req(url);
    req.setTransferTimeout(kHttpTimeoutMs);
    QNetworkReply* reply = m_nam->get(req);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(kHttpTimeoutMs);
    QObject::connect(reply,   &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout,        &loop, &QEventLoop::quit);
    timeout.start();
    loop.exec();

    bool ok = (reply->error() == QNetworkReply::NoError);
    reply->deleteLater();
    return ok;
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────
NodePlugin::NodePlugin(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{}

NodePlugin::~NodePlugin()
{
    if (m_process && m_startedByUs && m_process->state() != QProcess::NotRunning) {
        m_process->terminate();
        m_process->waitForFinished(5000);
        if (m_process->state() != QProcess::NotRunning)
            m_process->kill();
    }
}

void NodePlugin::initLogos(LogosAPI* api)
{
    logosAPI = api;
    appendLog(QStringLiteral("logos_node: initLogos called"));
}

// ── Config ────────────────────────────────────────────────────────────────────
QString NodePlugin::setNodeConfig(const QString& binaryPath,
                                   const QString& circuitsPath,
                                   const QString& configPath,
                                   const QString& dataDir)
{
    if (binaryPath.isEmpty())
        return errorJson(QStringLiteral("binaryPath is required"));
    if (circuitsPath.isEmpty())
        return errorJson(QStringLiteral("circuitsPath is required"));
    if (configPath.isEmpty())
        return errorJson(QStringLiteral("configPath is required"));
    if (dataDir.isEmpty())
        return errorJson(QStringLiteral("dataDir is required"));

    QSettings s;
    s.setValue(QLatin1String(kBinaryPathKey),   binaryPath);
    s.setValue(QLatin1String(kCircuitsPathKey),  circuitsPath);
    s.setValue(QLatin1String(kConfigPathKey),    configPath);
    s.setValue(QLatin1String(kDataDirKey),       dataDir);
    s.sync();

    appendLog(QStringLiteral("config saved"));
    return okJson();
}

QString NodePlugin::getNodeConfig() const
{
    QSettings s;
    QString binary   = s.value(QLatin1String(kBinaryPathKey)).toString();
    QString circuits = s.value(QLatin1String(kCircuitsPathKey)).toString();
    QString config   = s.value(QLatin1String(kConfigPathKey)).toString();
    QString dataDir  = s.value(QLatin1String(kDataDirKey)).toString();

    bool configured = !binary.isEmpty() && !circuits.isEmpty()
                   && !config.isEmpty() && !dataDir.isEmpty();

    QJsonObject o;
    o[QStringLiteral("binaryPath")]   = binary;
    o[QStringLiteral("circuitsPath")] = circuits;
    o[QStringLiteral("configPath")]   = config;
    o[QStringLiteral("dataDir")]      = dataDir;
    o[QStringLiteral("configured")]   = configured;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QString NodePlugin::getNodeUrl() const
{
    QSettings s;
    int port = s.value(QLatin1String(kHttpPortKey), 8080).toInt();
    return QStringLiteral("http://127.0.0.1:%1").arg(port);
}

// ── Start / Stop ──────────────────────────────────────────────────────────────
QString NodePlugin::startNode()
{
    // Check if already running (externally or previously started)
    if (isNodeResponding()) {
        if (!m_startedByUs)
            appendLog(QStringLiteral("node already running (external)"), QStringLiteral("info"));
        QJsonObject o;
        o[QStringLiteral("ok")]             = true;
        o[QStringLiteral("alreadyRunning")] = true;
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }

    QSettings s;
    QString binary   = s.value(QLatin1String(kBinaryPathKey)).toString();
    QString circuits = s.value(QLatin1String(kCircuitsPathKey)).toString();
    QString config   = s.value(QLatin1String(kConfigPathKey)).toString();
    QString dataDir  = s.value(QLatin1String(kDataDirKey)).toString();

    if (binary.isEmpty() || circuits.isEmpty() || config.isEmpty() || dataDir.isEmpty())
        return errorJson(QStringLiteral("node not configured — open settings"));

    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    m_process = new QProcess(this);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LOGOS_BLOCKCHAIN_CIRCUITS"), circuits);
    m_process->setProcessEnvironment(env);
    m_process->setWorkingDirectory(dataDir);
    m_process->setProcessChannelMode(QProcess::MergedChannels);

    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString out = QString::fromUtf8(m_process->readAllStandardOutput());
        for (const QString& line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            QString level = QStringLiteral("info");
            if (line.contains(QLatin1String("ERROR"), Qt::CaseInsensitive))
                level = QStringLiteral("error");
            else if (line.contains(QLatin1String("WARN"), Qt::CaseInsensitive))
                level = QStringLiteral("warn");
            appendLog(line, level);
        }
    });

    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        appendLog(QStringLiteral("node process exited: code %1").arg(code),
                  code == 0 ? QStringLiteral("info") : QStringLiteral("error"));
        m_startedByUs = false;
    });

    m_process->start(binary, {config});
    if (!m_process->waitForStarted(3000))
        return errorJson(QStringLiteral("failed to start process: ") + m_process->errorString());

    m_startedByUs = true;
    appendLog(QStringLiteral("node started (pid %1)").arg(m_process->processId()));
    return okJson();
}

QString NodePlugin::stopNode()
{
    if (!m_startedByUs)
        return errorJson(QStringLiteral("node was not started by this module"));

    if (!m_process || m_process->state() == QProcess::NotRunning)
        return errorJson(QStringLiteral("node is not running"));

    m_process->terminate();
    if (!m_process->waitForFinished(5000)) {
        m_process->kill();
        m_process->waitForFinished(2000);
    }

    m_startedByUs = false;
    appendLog(QStringLiteral("node stopped"));
    return okJson();
}

// ── Status ────────────────────────────────────────────────────────────────────
QString NodePlugin::getStatus()
{
    QSettings s;
    int port = s.value(QLatin1String(kHttpPortKey), 8080).toInt();
    QString nodeUrl = QStringLiteral("http://127.0.0.1:%1").arg(port);

    QJsonObject result;
    result[QStringLiteral("nodeUrl")]      = nodeUrl;
    result[QStringLiteral("startedByUs")] = m_startedByUs;

    QUrl url(nodeUrl + QStringLiteral("/cryptarchia/info"));
    QNetworkRequest req(url);
    req.setTransferTimeout(kHttpTimeoutMs);
    QNetworkReply* reply = m_nam->get(req);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(kHttpTimeoutMs);
    QObject::connect(reply,    &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout,         &loop, &QEventLoop::quit);
    timeout.start();
    loop.exec();

    if (reply->error() != QNetworkReply::NoError) {
        reply->deleteLater();
        result[QStringLiteral("running")] = false;
        return QJsonDocument(result).toJson(QJsonDocument::Compact);
    }

    QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        result[QStringLiteral("running")] = false;
        return QJsonDocument(result).toJson(QJsonDocument::Compact);
    }

    QJsonObject info = doc.object();
    result[QStringLiteral("running")]  = true;
    result[QStringLiteral("mode")]     = info[QStringLiteral("mode")];
    result[QStringLiteral("slot")]     = info[QStringLiteral("slot")];
    result[QStringLiteral("libSlot")]  = info[QStringLiteral("lib_slot")];
    result[QStringLiteral("height")]   = info[QStringLiteral("height")];
    result[QStringLiteral("tip")]      = info[QStringLiteral("tip")];
    result[QStringLiteral("lib")]      = info[QStringLiteral("lib")];

    return QJsonDocument(result).toJson(QJsonDocument::Compact);
}

// ── Log ───────────────────────────────────────────────────────────────────────
QString NodePlugin::getLog() const
{
    return formatLog();
}
