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
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

// ── QSettings key prefix ─────────────────────────────────────────────────────
static constexpr const char* kBinaryPathKey       = "logos-node/binaryPath";
static constexpr const char* kCircuitsPathKey      = "logos-node/circuitsPath";
static constexpr const char* kConfigPathKey        = "logos-node/configPath";
static constexpr const char* kDataDirKey           = "logos-node/dataDir";
static constexpr const char* kHttpPortKey          = "logos-node/httpPort";
static constexpr const char* kWalletPubKeyKey      = "logos-node/walletPubKey";
static constexpr const char* kZoneBoardDirKey      = "logos-node/zoneBoardDir";
static constexpr const char* kZoneBoardSessionKey  = "logos-node/zoneBoardSession";

// ── Zone topic decoder ────────────────────────────────────────────────────────
static QString decodeZoneTopic(const QString& hex)
{
    QByteArray raw = QByteArray::fromHex(hex.toLatin1());
    while (!raw.isEmpty() && raw.back() == '\0')
        raw.chop(1);
    static const QByteArray kPrefix = "logos:yolo:";
    if (raw.startsWith(kPrefix))
        raw = raw.mid(kPrefix.size());
    QString name = QString::fromUtf8(raw);
    if (name.isEmpty() || name.contains(QChar(0xFFFD)))
        return hex.left(13) + "...";
    return name;
}

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
    // Already started by us and process is still alive — don't double-launch
    if (m_startedByUs && m_process && m_process->state() == QProcess::Running) {
        QJsonObject o;
        o[QStringLiteral("ok")]             = true;
        o[QStringLiteral("alreadyRunning")] = true;
        return QJsonDocument(o).toJson(QJsonDocument::Compact);
    }

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

    // Derive runbook root: the node config lives at {root}/configs/{env}/node.yaml
    // All relative paths in the config (./db, ./state/...) resolve from the root.
    // run-node.sh does `cd {root}` before exec — we must match that.
    QDir configDir = QFileInfo(config).absoluteDir(); // configs/live/
    configDir.cdUp();                                      // configs/
    configDir.cdUp();                                      // root/
    const QString workDir = configDir.absolutePath();

    m_process = new QProcess(this);

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LOGOS_BLOCKCHAIN_CIRCUITS"), circuits);
    m_process->setProcessEnvironment(env);
    m_process->setWorkingDirectory(workDir);
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

    bool processAlive = m_startedByUs && m_process
                        && m_process->state() == QProcess::Running;

    QJsonObject result;
    result[QStringLiteral("nodeUrl")]       = nodeUrl;
    result[QStringLiteral("startedByUs")]  = m_startedByUs;
    result[QStringLiteral("processRunning")] = processAlive;

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

// ── Zone / wallet config ──────────────────────────────────────────────────────
QString NodePlugin::setZoneConfig(const QString& walletPubKey,
                                   const QString& zoneBoardDir,
                                   const QString& zoneBoardSession)
{
    QSettings s;
    s.setValue(QLatin1String(kWalletPubKeyKey),     walletPubKey);
    s.setValue(QLatin1String(kZoneBoardDirKey),      zoneBoardDir);
    s.setValue(QLatin1String(kZoneBoardSessionKey),
               zoneBoardSession.isEmpty() ? QStringLiteral("zone-board") : zoneBoardSession);
    s.sync();
    appendLog(QStringLiteral("zone config saved"));
    return okJson();
}

QString NodePlugin::getZoneConfig() const
{
    QSettings s;
    QJsonObject o;
    o[QStringLiteral("walletPubKey")]     = s.value(QLatin1String(kWalletPubKeyKey)).toString();
    o[QStringLiteral("zoneBoardDir")]     = s.value(QLatin1String(kZoneBoardDirKey)).toString();
    o[QStringLiteral("zoneBoardSession")] = s.value(QLatin1String(kZoneBoardSessionKey),
                                                     QStringLiteral("zone-board")).toString();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

// ── Balance ───────────────────────────────────────────────────────────────────
QString NodePlugin::getBalance()
{
    QSettings s;
    QString pubKey = s.value(QLatin1String(kWalletPubKeyKey)).toString().trimmed();
    if (pubKey.isEmpty())
        return errorJson(QStringLiteral("wallet public key not configured"));

    int port = s.value(QLatin1String(kHttpPortKey), 8080).toInt();
    QUrl url(QStringLiteral("http://127.0.0.1:%1/wallet/%2/balance").arg(port).arg(pubKey));
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
        QString err = reply->errorString();
        reply->deleteLater();
        return errorJson(QStringLiteral("balance unavailable: ") + err);
    }
    QByteArray body = reply->readAll();
    reply->deleteLater();

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(body, &err);
    if (err.error != QJsonParseError::NoError)
        return errorJson(QStringLiteral("invalid balance response"));

    QJsonObject result = doc.isObject() ? doc.object() : QJsonObject();
    result[QStringLiteral("ok")] = true;
    return QJsonDocument(result).toJson(QJsonDocument::Compact);
}

// ── Zone messages ─────────────────────────────────────────────────────────────
QString NodePlugin::getZoneMessages() const
{
    QSettings s;
    QString dirPath = s.value(QLatin1String(kZoneBoardDirKey)).toString().trimmed();
    if (dirPath.isEmpty())
        return errorJson(QStringLiteral("zone board dir not configured"));

    QJsonArray channels;
    QStringList seenTopics;

    QDir cacheDir(dirPath + QStringLiteral("/cache"));
    if (cacheDir.exists()) {
        const QStringList files = cacheDir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString& fname : files) {
            QString topic = fname;
            topic.chop(5); // remove .json
            seenTopics.append(topic);

            QJsonArray messages;
            QFile f(cacheDir.filePath(fname));
            if (f.open(QIODevice::ReadOnly)) {
                QJsonParseError pe;
                QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
                if (pe.error == QJsonParseError::NoError && doc.isArray())
                    messages = doc.array();
                f.close();
            }

            QJsonObject ch;
            ch[QStringLiteral("channel")]  = decodeZoneTopic(topic);
            ch[QStringLiteral("topic")]    = topic;
            ch[QStringLiteral("messages")] = messages;
            channels.append(ch);
        }
    }

    // Also add subscribed channels not yet in cache
    QFile subFile(dirPath + QStringLiteral("/subscriptions.json"));
    if (subFile.open(QIODevice::ReadOnly)) {
        QJsonParseError pe;
        QJsonDocument doc = QJsonDocument::fromJson(subFile.readAll(), &pe);
        subFile.close();
        if (pe.error == QJsonParseError::NoError && doc.isArray()) {
            for (const QJsonValue& v : doc.array()) {
                QString topic = v.toString();
                if (!topic.isEmpty() && !seenTopics.contains(topic)) {
                    seenTopics.append(topic);
                    QJsonObject ch;
                    ch[QStringLiteral("channel")]  = decodeZoneTopic(topic);
                    ch[QStringLiteral("topic")]    = topic;
                    ch[QStringLiteral("messages")] = QJsonArray();
                    channels.append(ch);
                }
            }
        }
    }

    // Inject own channel from channel.id (zone-board doesn't list it in subscriptions)
    QFile channelIdFile(dirPath + QStringLiteral("/channel.id"));
    if (channelIdFile.open(QIODevice::ReadOnly)) {
        QString raw = QString::fromUtf8(channelIdFile.readAll()).trimmed();
        channelIdFile.close();
        // channel.id contains "logos:yolo:name\0\0..." — strip prefix and nulls
        static const QString kPrefix = QStringLiteral("logos:yolo:");
        if (raw.startsWith(kPrefix)) raw = raw.mid(kPrefix.size());
        raw.remove(QChar('\0'));
        raw = raw.trimmed();
        // Convert back to hex topic to check if already in list
        QByteArray topicBytes = (QStringLiteral("logos:yolo:") + raw).toUtf8();
        topicBytes = topicBytes.left(32).leftJustified(32, '\0');
        QString topicHex = QString::fromLatin1(topicBytes.toHex());
        if (!raw.isEmpty() && !seenTopics.contains(topicHex)) {
            // Check if there's a cache file for it
            QJsonArray myMessages;
            QFile myCacheFile(cacheDir.filePath(topicHex + QStringLiteral(".json")));
            if (myCacheFile.open(QIODevice::ReadOnly)) {
                QJsonParseError pe;
                QJsonDocument doc = QJsonDocument::fromJson(myCacheFile.readAll(), &pe);
                if (pe.error == QJsonParseError::NoError && doc.isArray())
                    myMessages = doc.array();
                myCacheFile.close();
            }
            QJsonObject ch;
            ch[QStringLiteral("channel")]  = raw + QStringLiteral(" (you)");
            ch[QStringLiteral("topic")]    = topicHex;
            ch[QStringLiteral("messages")] = myMessages;
            // Insert at front so own channel appears first
            QJsonArray reordered;
            reordered.append(ch);
            for (const QJsonValue& v : channels) reordered.append(v);
            channels = reordered;
        }
    }

    QJsonObject result;
    result[QStringLiteral("ok")]       = true;
    result[QStringLiteral("channels")] = channels;
    return QJsonDocument(result).toJson(QJsonDocument::Compact);
}

// ── Zone publish / subscribe ──────────────────────────────────────────────────
QString NodePlugin::publishZoneMessage(const QString& message)
{
    QSettings s;
    QString session = s.value(QLatin1String(kZoneBoardSessionKey),
                               QStringLiteral("zone-board")).toString();
    QString text = message.trimmed();
    if (text.isEmpty())
        return errorJson(QStringLiteral("message is empty"));

    // Clear line, type message, submit — same as Python server
    int r1 = QProcess::execute(QStringLiteral("tmux"),
                                {QStringLiteral("send-keys"), QStringLiteral("-t"),
                                 session, QStringLiteral("Enter")});
    int r2 = QProcess::execute(QStringLiteral("tmux"),
                                {QStringLiteral("send-keys"), QStringLiteral("-t"),
                                 session, QStringLiteral("-l"), text});
    int r3 = QProcess::execute(QStringLiteral("tmux"),
                                {QStringLiteral("send-keys"), QStringLiteral("-t"),
                                 session, QStringLiteral("Enter")});

    if (r1 != 0 || r2 != 0 || r3 != 0)
        return errorJson(QStringLiteral("zone-board session not found: ") + session);

    appendLog(QStringLiteral("zone publish: ") + text.left(60));
    return okJson();
}

QString NodePlugin::getNodeLogs() const
{
    QSettings s;
    QString dataDir = s.value(QLatin1String(kDataDirKey)).toString().trimmed();
    if (dataDir.isEmpty())
        return errorJson(QStringLiteral("data dir not configured"));

    QDir logDir(dataDir + QStringLiteral("/logs"));
    if (!logDir.exists())
        return errorJson(QStringLiteral("log dir not found: ") + logDir.absolutePath());

    // Find latest-modified log file
    const QFileInfoList files = logDir.entryInfoList(QDir::Files, QDir::Time);
    if (files.isEmpty()) {
        // Disk log hasn't appeared yet — fall back to process stdout if we started it
        if (m_startedByUs && m_process && m_process->state() == QProcess::Running
                && !m_logBuffer.isEmpty()) {
            int start = qMax(0, m_logBuffer.size() - 80);
            QJsonArray arr;
            for (int i = start; i < m_logBuffer.size(); i++)
                arr.append(m_logBuffer[i].ts + QLatin1String(" ") + m_logBuffer[i].msg);
            QJsonObject r;
            r[QStringLiteral("ok")]    = true;
            r[QStringLiteral("file")]  = QStringLiteral("(process stdout)");
            r[QStringLiteral("lines")] = arr;
            return QJsonDocument(r).toJson(QJsonDocument::Compact);
        }
        return errorJson(QStringLiteral("no log files found"));
    }

    QFile f(files.first().absoluteFilePath());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return errorJson(QStringLiteral("cannot open log file"));

    static const QRegularExpression kAnsi(QStringLiteral("\\x1b\\[[0-9;]*[mGKHFJA-Z]"));

    // Collect last 80 lines
    QList<QString> lines;
    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine();
        line.remove(kAnsi);
        if (line.trimmed().isEmpty()) continue;
        if (lines.size() >= 80) lines.removeFirst();
        lines.append(line);
    }
    f.close();

    QJsonArray arr;
    for (const QString& l : lines) arr.append(l);

    QJsonObject result;
    result[QStringLiteral("ok")]    = true;
    result[QStringLiteral("file")]  = files.first().fileName();
    result[QStringLiteral("lines")] = arr;
    return QJsonDocument(result).toJson(QJsonDocument::Compact);
}

QString NodePlugin::subscribeZoneChannel(const QString& channel)
{
    QSettings s;
    QString session = s.value(QLatin1String(kZoneBoardSessionKey),
                               QStringLiteral("zone-board")).toString();
    QString name = channel.trimmed();
    if (name.isEmpty())
        return errorJson(QStringLiteral("channel name is empty"));

    QString cmd = QStringLiteral("/sub ") + name;
    int r1 = QProcess::execute(QStringLiteral("tmux"),
                                {QStringLiteral("send-keys"), QStringLiteral("-t"),
                                 session, QStringLiteral("-l"), cmd});
    int r2 = QProcess::execute(QStringLiteral("tmux"),
                                {QStringLiteral("send-keys"), QStringLiteral("-t"),
                                 session, QStringLiteral("Enter")});

    if (r1 != 0 || r2 != 0)
        return errorJson(QStringLiteral("zone-board session not found: ") + session);

    appendLog(QStringLiteral("zone subscribe: ") + name);
    return okJson();
}
