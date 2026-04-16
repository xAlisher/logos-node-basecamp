# Epic 5 — In-Process Node Start

**Branch:** `epic/in-process-node-start`
**Goal:** Click Start in Basecamp → node boots, syncs, logs appear — no tmux, no shell.

---

## Background / Why this is hard

The node binary resolves all paths **relative to the working directory**, not to the config file location. `run-node.sh` does `cd {runbook_root}` before exec, so:

| Config key | Value | Resolved (from runbook root) |
|---|---|---|
| `storage.backend.folder_name` | `./db` | `{root}/db/` |
| `state.base_folder` | `./state/live-v0.1.2` | `{root}/state/live-v0.1.2/` |
| `tracing.logger.file.directory` | `./state/live-v0.1.2/logs` | `{root}/state/live-v0.1.2/logs/` |

Our `startNode()` was setting `setWorkingDirectory(dataDir)` which is one level too deep (`state/live-v0.1.2/`). This causes:
- Logs written to a nested path that doesn't exist → `getNodeLogs()` returns empty
- Storage opens a fresh empty DB instead of the synced chain data
- The node effectively starts from scratch on every Basecamp-initiated launch

**Root dir derivation:** Config is at `{root}/configs/{env}/node.yaml` → go up 2 directories from the config file's directory. This matches the actual file layout and `run-node.sh` behaviour.

---

## Issues

### Issue #10 — Fix working directory in `startNode()`

**Files:** `src/plugin/NodePlugin.cpp`, `src/plugin/NodePlugin.h`

**Changes:**
1. Derive `workDir` as config file's grandparent directory:
   ```cpp
   QDir configDir = QFileInfo(configPath).absoluteDir();
   configDir.cdUp(); // configs/live → configs/
   configDir.cdUp(); // configs/ → root/
   QString workDir = configDir.absolutePath();
   m_process->setWorkingDirectory(workDir);
   ```
2. Prevent double-start: check `m_process->state() == QProcess::Running` before HTTP probe:
   ```cpp
   if (m_startedByUs && m_process && m_process->state() == QProcess::Running) {
       QJsonObject o;
       o["ok"] = true; o["alreadyRunning"] = true;
       return QJsonDocument(o).toJson(QJsonDocument::Compact);
   }
   ```

**Verification:** click Start → within ~60s logs appear in blockchain logs panel showing real chain output (not empty). DB at `{root}/db/` fills with RocksDB files. Stop → process exits cleanly.

---

### Issue #11 — Add `processRunning` field to `getStatus()` + QML "Starting…" state

**Problem:** From click to HTTP responding, the node takes 30–90 s. During this time `running=false` in QML so the status dot shows red / "Offline" and the Start button is still clickable.

**C++ change** (`NodePlugin.cpp — getStatus()`):
```cpp
bool processAlive = m_startedByUs && m_process
                    && m_process->state() == QProcess::Running;
result["processRunning"] = processAlive;
// existing http check sets result["running"] = true/false
```

**QML changes** (`plugins/node_ui/Main.qml`):
- Add `property bool nodeProcessRunning: false`
- In poll handler: `root.nodeProcessRunning = st.processRunning === true`
- `statusColor()`:
  ```js
  if (root.nodeProcessRunning && !root.nodeRunning) return root.warnAmber
  ```
- `statusLabel()`:
  ```js
  if (root.nodeProcessRunning && !root.nodeRunning) return "Starting…"
  ```
- Start button: `enabled: !root.nodeProcessRunning && !root.nodeRunning`

**Verification:** click Start → dot turns amber / "Starting…" → after 30–90 s turns green / "Online". Start button disabled during boot.

---

### Issue #12 — Process stdout in blockchain logs panel during startup

**Problem:** `getNodeLogs()` reads disk files. During the first 10–20 s after clicking Start, the node hasn't written its first log file yet (it takes a moment to open the file backend). The blockchain logs panel is empty while the node is booting.

**Fix:** In `getNodeLogs()`, when the log directory is empty but `m_startedByUs && m_process->state() == QProcess::Running`, fall back to the last 80 entries from `m_logBuffer`:
```cpp
if (files.isEmpty() && m_startedByUs && m_process
    && m_process->state() == QProcess::Running) {
    // return m_logBuffer tail as fallback
    int start = qMax(0, m_logBuffer.size() - 80);
    QJsonArray arr;
    for (int i = start; i < m_logBuffer.size(); i++)
        arr.append(m_logBuffer[i].ts + " " + m_logBuffer[i].msg);
    QJsonObject r;
    r["ok"] = true; r["file"] = "(process stdout)"; r["lines"] = arr;
    return QJsonDocument(r).toJson(QJsonDocument::Compact);
}
```

**Verification:** click Start → blockchain logs panel immediately shows node stdout (key/circuit loading messages) while waiting for the file backend to come up.

---

## Execution order

```
#10 → #11 → #12
```

#10 is the blocking fix — without the correct workdir the other two are irrelevant.

---

## Verification checklist

```
1. Stop any externally-running node
2. Click Start in Basecamp
3. Status dot: amber "Starting…" within 3s
4. Blockchain logs panel: shows stdout lines within 10s
5. After ~60s: status dot turns green "Online"
6. Slot / Height cards update every 3s
7. Blockchain logs panel shows fresh disk log lines
8. Click Stop → process exits, dot turns red, logs stop updating
9. Start node manually in tmux → open Basecamp → Stop button greyed
```
