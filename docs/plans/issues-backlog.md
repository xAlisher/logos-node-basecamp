# Issue Backlog — logos-node-basecamp

Ordered simplest → hardest. Each issue is self-contained and shippable.

---

## #15 — Peer count stat card

**Effort:** XS (30 min)

`GET /network/peers` returns `{"n_peers":13,"n_connections":13,"peer_id":"...","listen_addresses":[...]}`.
Add a 6th stat card in the Node tab: **Peers** showing `n_peers`.
Poll in the existing 3s timer alongside `getStatus()`.

**C++ change:** new `Q_INVOKABLE QString getPeerInfo()` → HTTP GET `/network/peers`, return JSON.

**QML change:** add `nodePeers: 0` property; add Peers card to the Repeater model.

**Acceptance:** Peers card shows 13 (or live count) when node is Online.

---

## #16 — Disk usage stat card

**Effort:** XS (30 min)

Show DB size in the Node tab. No HTTP call — read from disk.

**C++ change:** new `Q_INVOKABLE QString getDiskUsage()` → walk `{dataDir}/db/` with `QDirIterator`,
sum file sizes, return `{"bytes": N, "human": "1.2 GB"}`.

**QML change:** add `nodeDiskUsage: "—"` property; add to stat cards row or show below chain view.
Update every 30s (not every 3s — disk scan is slow).

**Acceptance:** Shows e.g. "1.2 GB" next to other stats; updates as chain grows.

---

## #17 — Auto-start on boot toggle

**Effort:** S (1 h)

Settings panel: add a toggle "Start on login" that enables/disables the existing
`logos-node.service` systemd user unit.

**C++ change:** new `Q_INVOKABLE QString getAutoStart()` → `systemctl --user is-enabled logos-node`
→ returns `{"enabled": true/false}`.
New `Q_INVOKABLE QString setAutoStart(bool enable)` →
`systemctl --user enable/disable logos-node` via `QProcess::execute`.

**QML change:** toggle switch in Settings panel, loads state on open, saves on toggle.

**Note:** The service file is expected at `~/logos-blockchain-runbook/logos-node.service`.
If not installed yet: `systemctl --user link {path}` first.

**Acceptance:** Toggle ON → node starts on next login. Toggle OFF → it doesn't.

---

## #18 — Historical log browser

**Effort:** S (1–2 h)

Node tab currently shows only the latest log file. There are many dated files in
`{dataDir}/logs/`. Add a file selector above the log panel.

**C++ change:** new `Q_INVOKABLE QString listLogFiles()` → `QDir` scan of `{dataDir}/logs/`,
return `[{"name":"logos-blockchain.2026-04-16-15","path":"..."}]` sorted newest first.

**QML change:** horizontal Flickable of pill buttons above the log panel (like zone channel tabs).
Clicking a pill sets `selectedLogFile` and passes it to `getNodeLogs(file)`.
`getNodeLogs()` gains an optional `file` parameter (empty = latest).

**Acceptance:** Can scroll back through previous log sessions; current session auto-selected.

---

## #19 — Finality labels on zone messages

**Effort:** M (2–3 h)

Zone messages have a `block_id` field. The node's `lib` hash is the latest finalized block.
A message is finalized when its block is at or below LIB slot.

**API:** `GET /cryptarchia/blocks/{block_id}` — check if this endpoint exists and returns slot.
If it does: compare message block slot ≤ `lib_slot` → finalized.
If not: fall back to checking whether block_id appears in node logs before the current lib.

**C++ change:** in `getZoneMessages()`, for each message with a `block_id`, optionally fetch
its slot and compare to current lib_slot (cached from last `getStatus()` call).
Add `"finalized": true/false` to each message object.

**QML change:** in message delegate, show a small `✓` checkmark (green) or `⏳` (amber)
next to the block_id when `finalized` is known.

**Acceptance:** Messages in finalized blocks show green ✓; pending show ⏳.

---

## #20 — Config profile switcher

**Effort:** M (2–3 h)

The runbook has multiple configs under `configs/`. Let users switch between them
without editing the Settings text field manually.

**C++ change:** new `Q_INVOKABLE QString listConfigs()` → scan parent directory of the current
config file for `*.yaml` files, return `[{"name":"node.yaml","path":"..."}]`.

**QML change:** In Settings, replace the "Config File" free-text field with a dropdown
(ComboBox) populated from `listConfigs()`. Selecting one calls `setNodeConfig(...)` with
the new config path. Show the free-text field as an override below the dropdown.

**Acceptance:** ComboBox lists `node.yaml`, `node-pre-0.1.2.yaml`; switching restarts with
the new config on next Start.

---

## #21 — Node version picker

**Effort:** M (2–3 h)

Multiple node binaries in `artifacts/node/`. Let users switch without editing settings.

**C++ change:** new `Q_INVOKABLE QString listNodeBinaries()` → scan parent directory of
the current binary path for executable files, return
`[{"name":"logos-blockchain-node-0.1.1rc2","path":"...","active":bool}]`.
Run `{binary} --version` (or check filename) for version string.

**QML change:** In Settings, replace "Node Binary" text field with a picker:
list of version pills, active one highlighted. Selecting calls `setNodeConfig(...)` with
the new binary path.

**Acceptance:** Shows available versions; switching takes effect on next Start.

---

## #22 — Inscribe panel

**Effort:** M–L (3–4 h)

The `logos-blockchain-node inscribe` subcommand publishes text inscriptions as zone blocks.

**CLI:** `logos-blockchain-node inscribe --node-url http://127.0.0.1:8080 --signing-key {key} "{text}"`

**C++ change:** new `Q_INVOKABLE QString inscribe(const QString& text, const QString& signingKey)` →
build args, run via `QProcess`, capture output, return `{"ok":true,"txId":"..."}` or error.
Signing key loaded from config (KMS section of node.yaml has known keys).

**QML change:** New tab or panel in Zone tab: "Inscribe" compose area + key selector
(populated from config keys). Shows TX id / confirmation after submit.

**Acceptance:** Text entered → inscribe runs → TX id appears → message shows up in zone board.

---

## #23 — Node update flow

**Effort:** L (4–6 h)

Check GitHub releases for new node binary, download, verify, swap.

**C++ changes:**
- `Q_INVOKABLE QString checkForUpdate()` → HTTP GET GitHub releases API
  (`https://api.github.com/repos/logos-blockchain/logos-blockchain/releases/latest`),
  compare tag to current binary version string → return `{"current":"0.1.2","latest":"0.1.3","updateAvailable":true}`.
- `Q_INVOKABLE QString downloadUpdate(const QString& version)` →
  download `logos-blockchain-node-linux-x86_64-{version}.tar.gz` to `artifacts/`,
  extract, replace binary, report progress via `eventResponse` signal.

**QML change:** "Check for updates" button in Settings. If update available: banner with
version + Download button. Progress bar during download. Confirmation before swap.
Never auto-swap while node is running (show warning).

**Acceptance:** New release available → user clicks Download → binary swapped → Start uses new version.

---

## Execution order

```
#15 Peer count      ← start here, 30 min
#16 Disk usage      ← 30 min
#17 Auto-start      ← 1 h
#18 Log browser     ← 1–2 h
#19 Finality labels ← 2–3 h
#20 Config switcher ← 2–3 h
#21 Version picker  ← 2–3 h
#22 Inscribe        ← 3–4 h
#23 Node update     ← 4–6 h
```
