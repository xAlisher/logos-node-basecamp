#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QProcess>
#include <QNetworkAccessManager>

#include "core/interface.h"

class NodePlugin : public QObject, public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "org.logos.NodeModuleInterface" FILE "plugin_metadata.json")
    Q_INTERFACES(PluginInterface)

public:
    explicit NodePlugin(QObject* parent = nullptr);
    ~NodePlugin() override;

    QString name()    const override { return QStringLiteral("logos_node"); }
    QString version() const override { return QStringLiteral("0.1.0"); }

    Q_INVOKABLE void    initLogos(LogosAPI* api);

    // Config — all four paths required to start the node
    Q_INVOKABLE QString setNodeConfig(const QString& binaryPath,
                                      const QString& circuitsPath,
                                      const QString& configPath,
                                      const QString& dataDir);
    Q_INVOKABLE QString getNodeConfig() const;

    // Lifecycle — Option C: connect to existing or start new
    Q_INVOKABLE QString startNode();
    Q_INVOKABLE QString stopNode();

    // Status from GET /cryptarchia/info + process state
    Q_INVOKABLE QString getStatus();

    // Last 200 lines of node stdout/stderr
    Q_INVOKABLE QString getLog() const;

    // For beacon and other modules — returns configured node URL
    Q_INVOKABLE QString getNodeUrl() const;

    // Zone / wallet config (separate from node binary config)
    Q_INVOKABLE QString setZoneConfig(const QString& walletPubKey,
                                       const QString& zoneBoardBinaryPath,
                                       const QString& zoneBoardDir);
    Q_INVOKABLE QString getZoneConfig() const;

    // Zone-board process lifecycle
    Q_INVOKABLE QString startZoneBoard();
    Q_INVOKABLE QString stopZoneBoard();

    // LGO balance from GET /wallet/{pubkey}/balance
    Q_INVOKABLE QString getBalance();

    // Zone messages from zone-board cache directory
    Q_INVOKABLE QString getZoneMessages() const;
    Q_INVOKABLE QString publishZoneMessage(const QString& message);
    Q_INVOKABLE QString subscribeZoneChannel(const QString& channel);

    // Last 80 lines from the node's on-disk log files (ANSI stripped)
    Q_INVOKABLE QString getNodeLogs() const;

signals:
    void eventResponse(const QString& eventName, const QVariantList& data);

private:
    static QString errorJson(const QString& msg);
    static QString okJson();

    bool    isNodeResponding() const;
    void    appendLog(const QString& line, const QString& level = QStringLiteral("info"));
    QString formatLog() const;

    struct LogEntry {
        QString ts;
        QString msg;
        QString level;
    };

    QProcess*              m_process             = nullptr;
    QProcess*              m_zoneBoardProcess    = nullptr;
    QNetworkAccessManager* m_nam                 = nullptr;
    bool                   m_startedByUs         = false;
    bool                   m_zoneBoardStartedByUs = false;
    QList<LogEntry>        m_logBuffer;    // capped at 200
    static constexpr int   kMaxLogLines   = 200;
    static constexpr int   kHttpTimeoutMs = 2000;
};
