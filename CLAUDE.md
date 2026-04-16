# logos-node-basecamp — Claude Code Instructions

> Read `docs/plans/node-implementation.md` first. It contains the implementation plan,
> issue breakdown, and verification steps.

## Identity & Protocols

You are **Fergie**. Protocols load via `.claude/rules/`. tmux-bridge labels:
`fergie@logos-node-basecamp`, `senty@logos-node-basecamp`.

**Alisher sign-off required for:**
- Destructive operations (rm -rf, force push, drop QSettings)
- API contract changes visible to other modules (e.g. `getNodeUrl` return format)
- Major architectural pivots

Everything else: agents handle autonomously.

---

## Project Context

**logos-node-basecamp** — Logos blockchain node manager for Basecamp.
Wraps `logos-blockchain-node` binary via `QProcess`. Shows sync status, height, slot,
live log. Start/Stop button. Connects to externally-running node if already up.

**Status:** Issue #1 (scaffold) in progress.

**Sibling modules this integrates with:**
- `beacon-basecamp` — calls `getNodeUrl()` to know where to post inscriptions
- `stash-basecamp` — independent, no direct integration

---

## Code Style & Patterns

### Q_INVOKABLE — always return JSON strings

```cpp
Q_INVOKABLE QString getStatus() {
    QJsonObject o;
    o["running"] = true;
    o["slot"] = 259368;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
```

Never return `bool`, `int`, or QVariant — they don't cross the QML bridge reliably.

### QProcess — always MergedChannels, always SIGTERM first

```cpp
m_process->setProcessChannelMode(QProcess::MergedChannels);
m_process->terminate();
if (!m_process->waitForFinished(5000)) m_process->kill();
```

### HTTP status poll — synchronous pattern (QEventLoop + QTimer timeout)

```cpp
QNetworkReply* reply = m_nam->get(req);
QEventLoop loop;
QTimer timeout; timeout.setSingleShot(true); timeout.setInterval(2000);
connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
timeout.start(); loop.exec();
bool ok = (reply->error() == QNetworkReply::NoError);
reply->deleteLater();
```

### Log buffer — circular, capped at 200 lines

Append to `m_logBuffer`; remove first when at cap. Level heuristic: "ERROR" → error,
"WARN" → warn, else info.

---

## Build & Test Workflow

```bash
# Build
cmake -B build && cmake --build build -j$(nproc)

# Test
cd build && ctest --output-on-failure

# Install (LogosApp + mirrors to LogosBasecamp)
cmake --install build

# Kill + relaunch Basecamp
pkill -9 -f "LogosBasecamp.elf"; sleep 1
~/logos-basecamp-current.AppImage &
```

---

## Module Install Paths

```
~/.local/share/Logos/LogosApp/
├── modules/logos_node/
│   ├── node_plugin.so
│   ├── manifest.json / metadata.json / plugin_metadata.json / variant
└── plugins/node_ui/
    ├── Main.qml / manifest.json / metadata.json / variant
    └── icons/Node_sidebar.png
```

CMake install also mirrors to `LogosBasecamp/` automatically.

---

## Common Pitfalls (from stash/keycard lessons)

- **`background: null` on TextEdit** — silent QML load failure. Only valid on TextField/TextArea.
- **Clipboard TextEdit helper must be at root level** — not inside nested Rectangle.
- **ListModel not JS array** — `model.get(i)` only works on ListModel.
- **variant file required** — `linux-amd64` must be in BOTH module and plugin dirs.
- **patchelf RUNPATH** — required so Qt libs resolve outside Nix environment.
- **startedByUs flag** — never kill a node we didn't start.
- **pollBusy guard** — callModule blocks QML thread; Timer re-enters without guard.

---

## File Organization

```
logos-node-basecamp/
├── src/plugin/
│   ├── NodePlugin.h / NodePlugin.cpp
│   └── plugin_metadata.json
├── modules/logos_node/
│   ├── manifest.json / metadata.json / plugin_metadata.json / variant
├── plugins/node_ui/
│   ├── Main.qml / manifest.json / metadata.json / variant
│   └── icons/
├── assets/icons/Node_sidebar.png
├── tests/
│   ├── test_node_plugin.cpp
│   └── logos_api_stub.cpp
├── docs/
│   ├── plans/node-implementation.md
│   └── retro-log.md
├── CMakeLists.txt
├── CLAUDE.md
└── CODEX.md
```

---

## Issue Tracking

Issues tracked on the private GitHub repo. Branch per issue:
```bash
git checkout -b issue-N-brief-description
```
Never work directly on main. Senty reviews before merge.

Current issue breakdown (see `docs/plans/node-implementation.md` for full checklists):

| # | Title | Status |
|---|-------|--------|
| 1 | Project scaffold | in-progress |
| 2 | Config invokables | pending |
| 3 | QProcess lifecycle | pending |
| 4 | HTTP status poll | pending |
| 5 | Plugin skeleton + settings | pending |
| 6 | Status display | pending |
| 7 | Activity log + start/stop | pending |
| 8 | Unit tests — config | pending |
| 9 | Integration test — FakeNodeServer | pending |
