# logos-node-basecamp

> This is a personal, experimental hobby project. It is not an official Logos product. Not audited.


A [Logos Basecamp](https://github.com/logos-co) sidebar plugin that manages a
`logos-blockchain-node` process from the UI. Start and stop the node, watch
sync progress, and browse live logs — all without touching a terminal.

## Features

- **One-click start / stop** — launches `logos-blockchain-node` as a child
  process with the correct working directory and environment
- **Live sync status** — polls `GET /cryptarchia/info` every 3 s; shows mode
  (Online / Starting / Offline), slot, LIB slot, block height, tip hash
- **Blockchain log viewer** — streams stdout/stderr in real time; falls back to
  process output during startup before log files appear on disk
- **Zone board integration** — starts/stops `zone-board` alongside the node,
  publishes messages and manages channel subscriptions via stdin
- **External-node detection** — connects to an already-running node without
  killing it; Stop button is greyed out when the node was not started by
  Basecamp
- **LGO balance** — reads wallet balance from `GET /wallet/{pubkey}/balance`
- **Cross-module API** — exposes `getNodeUrl()` so sibling modules (e.g.
  `beacon-basecamp`) can reach the node without duplicating config

## Requirements

| Dependency | Version |
|---|---|
| Qt | 6.9.x |
| Logos C++ SDK (`liblogos_sdk.a`) | 0.1.x |
| Logos liblogos headers | 0.1.x |
| CMake | 3.28+ |
| patchelf | any (for RUNPATH patching) |

SDK and headers are resolved from Nix store paths by default. Override with
environment variables:

```
LOGOS_CPP_SDK_ROOT=/path/to/logos-cpp-sdk
LOGOS_LIBLOGOS_HEADERS=/path/to/logos-liblogos-headers/include
```

## Build

```bash
cmake -B build
cmake --build build -j$(nproc)
```

## Test

```bash
cd build && ctest --output-on-failure
```

Tests use an in-process `FakeNodeServer` (QTcpServer) — no real node required.

## Install

```bash
cmake --install build
```

Installs to:

```
~/.local/share/Logos/LogosApp/
├── modules/logos_node/      ← node_plugin.so + manifests
└── plugins/node_ui/         ← Main.qml + manifests + icon
```

Also mirrors to `LogosBasecamp/` automatically.

After install, kill and relaunch Basecamp:

```bash
pkill -9 -f "LogosBasecamp.elf"
~/logos-basecamp-current.AppImage &
```

## Configuration

Open the Settings panel in the Node tab and fill in:

| Field | Description |
|---|---|
| Node Binary | Path to `logos-blockchain-node` executable |
| Circuits Path | Path to circuit files (`LOGOS_BLOCKCHAIN_CIRCUITS`) |
| Config File | Path to `node.yaml` (e.g. `configs/live/node.yaml`) |
| Data Dir | Runbook root directory (working dir for the process) |
| Zone Board Binary | Path to `zone-board` executable |
| Zone Board Dir | Directory for zone-board cache and logs |
| Wallet Public Key | Public key for balance queries |

## Architecture

```
NodePlugin (C++ QObject / PluginInterface)
  ├── QProcess  → logos-blockchain-node
  ├── QProcess  → zone-board (stdin IPC)
  └── QNetworkAccessManager  → HTTP polls to /cryptarchia/info

QML (Main.qml)
  ├── Node tab   — status cards, sync bar, start/stop, blockchain logs
  └── Zone tab   — channel tabs, message list, compose + subscribe
```

All invokables return compact JSON strings. Status is polled on a 3 s timer
with a `pollBusy` re-entrancy guard to prevent QML thread blocking.

## Module API

Other Basecamp modules can call into this plugin via the Logos IPC bridge:

```js
logos.callModule("logos_node", "getNodeUrl", [])
// → "http://127.0.0.1:8080"

logos.callModule("logos_node", "getStatus", [])
// → {"running":true,"mode":"Online","slot":259368,"libSlot":258630,
//    "height":13326,"tip":"5321d0...","nodeUrl":"http://127.0.0.1:8080",
//    "startedByUs":true,"processRunning":true}
```

## License

MIT
