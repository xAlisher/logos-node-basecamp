# Epic 6 — No Tmux: Basecamp Owns All Processes

**Branch:** `epic/in-process-node-start` (continuing)
**Goal:** Eliminate tmux entirely. Basecamp starts/stops both `logos-blockchain-node`
and `zone-board` as QProcess children, sends commands via stdin.

---

## How zone-board works

```
zone-board --node-url http://127.0.0.1:8080 --data-dir {zoneBoardDir}
```

- Interactive CLI: reads commands from stdin, line by line
- Publish a message: write `{text}\n` to stdin
- Subscribe to channel: write `/sub {channel}\n` to stdin
- Connects to node HTTP; auto-reconnects when node is unavailable
- Writes its own log to `{zoneBoardDir}/zone-board.log`
- Stores subscriptions, channel.id, cache in `{zoneBoardDir}/`

We replace `tmux send-keys` calls with `m_zoneBoardProcess->write(...)`.

---

## Settings changes

| Old | New |
|-----|-----|
| "Zone Board Tmux Session" field + QSettings key | Removed |
| — | "Zone Board Binary" field + `logos-node/zoneBoardBinaryPath` key |

---

## Issue #13 — NodePlugin: manage zone-board via QProcess

**NodePlugin.h additions:**
```cpp
Q_INVOKABLE QString startZoneBoard();
Q_INVOKABLE QString stopZoneBoard();
Q_INVOKABLE QString getZoneBoardStatus() const;

QProcess* m_zoneBoardProcess    = nullptr;
bool      m_zoneBoardStartedByUs = false;
```

**startZoneBoard():**
```cpp
QString binary  = s.value(kZoneBoardBinaryKey).toString();
QString dataDir = s.value(kZoneBoardDirKey).toString();
QString nodeUrl = getNodeUrl();

m_zoneBoardProcess = new QProcess(this);
m_zoneBoardProcess->setProcessChannelMode(QProcess::MergedChannels);
connect(m_zoneBoardProcess, &QProcess::readyReadStandardOutput, ...capture to log...);
connect(m_zoneBoardProcess, &QProcess::finished, ...reset m_zoneBoardStartedByUs...);

m_zoneBoardProcess->start(binary, {"--node-url", nodeUrl, "--data-dir", dataDir});
```

zone-board reconnects gracefully when the node isn't ready yet — safe to start
immediately alongside the node.

**publishZoneMessage()** (replaces tmux):
```cpp
m_zoneBoardProcess->write("\n");
m_zoneBoardProcess->write((text + "\n").toUtf8());
```

**subscribeZoneChannel()** (replaces tmux):
```cpp
m_zoneBoardProcess->write(("/sub " + name + "\n").toUtf8());
```

**Auto-start:** `startNode()` calls `startZoneBoard()` after launching the node process.
**Auto-stop:** `stopNode()` calls `stopZoneBoard()` before stopping the node.
**Destructor:** terminate both processes if we started them.

---

## Issue #14 — QML: remove tmux session field, add zone-board binary field

**Settings panel changes:**
- Remove "Zone Board Tmux Session" TextInput
- Add "Zone Board Binary" TextInput (path to zone-board executable)
- Save button: call `setZoneConfig([walletPubKey, zoneBoardBinaryPath, zoneBoardDir])`

**Note:** `setZoneConfig()` signature changes — drop `zoneBoardSession`, add `zoneBoardBinaryPath`.

---

## Startup sequence

```
User clicks Start
  → startNode()
    → QProcess: logos-blockchain-node (runbook root as workdir)
    → startZoneBoard()
      → QProcess: zone-board --node-url ... --data-dir ...
        (reconnects automatically until node HTTP is ready)
  → status dot: amber "Starting…"
  → node HTTP responds → dot: green "Online"
  → zone-board syncing channels in background
```

---

## Verification

```
1. Stop tmux zone-board session (tmux kill-session -t zone-board)
2. Click Start in Basecamp
3. Activity log shows zone-board stdout lines
4. Zone tab: existing subscribed channels load from cache
5. Publish message → activity log shows send
6. Subscribe to new channel → appears in channel tabs after next poll
7. Click Stop → both node and zone-board processes exit
8. pkill tmux → Basecamp still fully functional
```
