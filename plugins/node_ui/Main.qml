import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Rectangle {
    id: root
    color: "#0A0A0A"
    anchors.fill: parent

    // ── Palette ───────────────────────────────────────────────────────────────
    readonly property color bgColor:       "#0A0A0A"
    readonly property color panelColor:    "#111111"
    readonly property color borderColor:   "#1E1E1E"
    readonly property color textPrimary:   "#E5E7EB"
    readonly property color textSecondary: "#9CA3AF"
    readonly property color textDisabled:  "#4B5563"
    readonly property color successGreen:  "#22C55E"
    readonly property color warnAmber:     "#F59E0B"
    readonly property color errorRed:      "#EF4444"
    readonly property color accentBlue:    "#38BDF8"

    // ── State ─────────────────────────────────────────────────────────────────
    property bool   settingsOpen:  false
    property bool   pollBusy:      false
    property int    logSeenCount:  0
    property bool   nodeRunning:   false
    property bool   startedByUs:   false
    property string nodeMode:      ""
    property int    nodeSlot:      0
    property int    nodeLibSlot:   0
    property int    nodeHeight:    0

    // ── callModuleParse — double-JSON unwrap ──────────────────────────────────
    function callModuleParse(raw) {
        try {
            var tmp = JSON.parse(raw)
            if (typeof tmp === 'string') {
                try { return JSON.parse(tmp) } catch(e) { return tmp }
            }
            return tmp
        } catch(e) { return null }
    }

    function statusColor() {
        if (!root.nodeRunning)          return root.errorRed
        if (root.nodeMode === "Online") return root.successGreen
        return root.warnAmber
    }

    function statusLabel() {
        if (!root.nodeRunning)          return "Offline"
        if (root.nodeMode === "Online") return "Online"
        return root.nodeMode || "Starting…"
    }

    // Root-level clipboard helper — must NOT be inside nested Rectangle (qml-patterns.md)
    TextEdit { id: clipHelper; visible: false }

    // ── Poll timer ────────────────────────────────────────────────────────────
    Timer {
        interval: 3000; running: true; repeat: true
        onTriggered: {
            if (root.pollBusy) return
            root.pollBusy = true

            var st = root.callModuleParse(logos.callModule("logos_node", "getStatus", []))
            if (st) {
                root.nodeRunning = st.running     === true
                root.startedByUs = st.startedByUs === true
                root.nodeMode    = st.mode    || ""
                root.nodeSlot    = st.slot    || 0
                root.nodeLibSlot = st.libSlot || 0
                root.nodeHeight  = st.height  || 0
            }

            var logArr = root.callModuleParse(logos.callModule("logos_node", "getLog", []))
            if (Array.isArray(logArr) && logArr.length > root.logSeenCount) {
                for (var i = root.logSeenCount; i < logArr.length; i++) {
                    var e = logArr[i]
                    if (logModel.count >= 200) logModel.remove(0)
                    logModel.append({ ts: "[" + (e.ts || "") + "]",
                                      msg: e.msg || "", level: e.level || "info" })
                }
                root.logSeenCount = logArr.length
                logListView.positionViewAtEnd()
            }

            root.pollBusy = false
        }
    }

    // ── Load config on open ───────────────────────────────────────────────────
    Component.onCompleted: {
        if (typeof logos === "undefined" || !logos.callModule) return
        var cfg = root.callModuleParse(logos.callModule("logos_node", "getNodeConfig", []))
        if (cfg) {
            binaryField.text   = cfg.binaryPath   || ""
            circuitsField.text = cfg.circuitsPath || ""
            configField.text   = cfg.configPath   || ""
            dataDirField.text  = cfg.dataDir      || ""
        }
    }

    // ── Layout ────────────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        // ── Toolbar ───────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Rectangle { width: 8; height: 8; radius: 4; color: root.statusColor() }

            Text { text: root.statusLabel(); color: root.textSecondary; font.pixelSize: 12 }

            Text {
                visible: root.nodeRunning
                text: "h:" + root.nodeHeight + "  slot:" + root.nodeSlot
                color: root.textDisabled; font.pixelSize: 11; font.family: "monospace"
            }

            Item { Layout.fillWidth: true }

            Rectangle {
                width: 56; height: 24; radius: 4
                color: "transparent"
                border.color: root.nodeRunning ? root.errorRed : root.successGreen
                visible: root.nodeRunning ? root.startedByUs : true
                Text {
                    anchors.centerIn: parent
                    text: root.nodeRunning ? "Stop" : "Start"
                    color: root.nodeRunning ? root.errorRed : root.successGreen
                    font.pixelSize: 11
                }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.nodeRunning) {
                            logos.callModule("logos_node", "stopNode", [])
                        } else {
                            var r = root.callModuleParse(
                                logos.callModule("logos_node", "startNode", []))
                            if (r && r.error)
                                logModel.append({ ts: "[now]",
                                                  msg: "start error: " + r.error,
                                                  level: "error" })
                        }
                    }
                }
            }

            Rectangle {
                width: 24; height: 24; radius: 4; color: "transparent"
                border.color: root.settingsOpen ? root.accentBlue : root.borderColor
                Text { anchors.centerIn: parent; text: "⚙"
                       color: root.settingsOpen ? root.accentBlue : root.textSecondary
                       font.pixelSize: 12 }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: root.settingsOpen = !root.settingsOpen
                }
            }
        }

        // ── Sync progress bar ─────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true; height: 3; radius: 2; color: root.borderColor
            visible: root.nodeRunning && root.nodeMode !== "Online" && root.nodeSlot > 0
            Rectangle {
                height: parent.height; radius: parent.radius; color: root.warnAmber
                width: root.nodeSlot > 0
                       ? Math.min(1.0, root.nodeLibSlot / root.nodeSlot) * parent.width : 0
            }
        }

        // ── Settings panel ────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true
            color: root.panelColor; border.color: root.borderColor; radius: 4
            visible: root.settingsOpen
            height: visible ? settingsInner.implicitHeight + 20 : 0

            ColumnLayout {
                id: settingsInner
                anchors { left: parent.left; right: parent.right; top: parent.top }
                anchors.margins: 10
                spacing: 8

                // Node Binary
                Text { text: "Node Binary"; color: root.textSecondary; font.pixelSize: 10 }
                Rectangle {
                    Layout.fillWidth: true; height: 26
                    color: "#0A0A0A"; border.color: root.borderColor; radius: 3
                    TextInput {
                        id: binaryField
                        anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.textPrimary; font.pixelSize: 11
                        font.family: "monospace"; clip: true
                        Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                               text: parent.text.length === 0 ? "/path/to/logos-blockchain-node" : ""
                               color: root.textDisabled; font.pixelSize: 11; font.family: "monospace" }
                    }
                }

                // Circuits Dir
                Text { text: "Circuits Dir"; color: root.textSecondary; font.pixelSize: 10 }
                Rectangle {
                    Layout.fillWidth: true; height: 26
                    color: "#0A0A0A"; border.color: root.borderColor; radius: 3
                    TextInput {
                        id: circuitsField
                        anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.textPrimary; font.pixelSize: 11
                        font.family: "monospace"; clip: true
                        Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                               text: parent.text.length === 0 ? "/path/to/logos-blockchain-circuits-vX-linux-x86_64" : ""
                               color: root.textDisabled; font.pixelSize: 11; font.family: "monospace" }
                    }
                }

                // Config File
                Text { text: "Config File"; color: root.textSecondary; font.pixelSize: 10 }
                Rectangle {
                    Layout.fillWidth: true; height: 26
                    color: "#0A0A0A"; border.color: root.borderColor; radius: 3
                    TextInput {
                        id: configField
                        anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.textPrimary; font.pixelSize: 11
                        font.family: "monospace"; clip: true
                        Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                               text: parent.text.length === 0 ? "/path/to/node.yaml" : ""
                               color: root.textDisabled; font.pixelSize: 11; font.family: "monospace" }
                    }
                }

                // Data Dir
                Text { text: "Data Dir"; color: root.textSecondary; font.pixelSize: 10 }
                Rectangle {
                    Layout.fillWidth: true; height: 26
                    color: "#0A0A0A"; border.color: root.borderColor; radius: 3
                    TextInput {
                        id: dataDirField
                        anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                        verticalAlignment: TextInput.AlignVCenter
                        color: root.textPrimary; font.pixelSize: 11
                        font.family: "monospace"; clip: true
                        Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter
                               text: parent.text.length === 0 ? "/path/to/state/live" : ""
                               color: root.textDisabled; font.pixelSize: 11; font.family: "monospace" }
                    }
                }

                // Save
                Rectangle {
                    Layout.alignment: Qt.AlignRight
                    width: 56; height: 24; radius: 4
                    color: "transparent"; border.color: root.successGreen
                    Text { anchors.centerIn: parent; text: "Save"
                           color: root.successGreen; font.pixelSize: 11 }
                    MouseArea {
                        anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            logos.callModule("logos_node", "setNodeConfig", [
                                binaryField.text, circuitsField.text,
                                configField.text,  dataDirField.text
                            ])
                            root.settingsOpen = false
                        }
                    }
                }
            }
        }

        // ── Activity log ──────────────────────────────────────────────────────
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: true
            color: "#0D0D0D"; border.color: root.borderColor; radius: 4

            Rectangle {
                z: 10
                anchors { top: parent.top; right: parent.right; margins: 4 }
                width: 22; height: 22; radius: 3
                color: "transparent"; border.color: root.borderColor
                visible: logModel.count > 0

                Text { anchors.centerIn: parent
                       text: copyFeedback.running ? "✓" : "⎘"
                       color: copyFeedback.running ? root.successGreen : root.textDisabled
                       font.pixelSize: 11 }

                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var text = ""
                        for (var i = 0; i < logModel.count; i++) {
                            var e = logModel.get(i)
                            text += e.ts + " " + e.msg + "\n"
                        }
                        clipHelper.text = text
                        clipHelper.selectAll()
                        clipHelper.copy()
                        copyFeedback.restart()
                    }
                }
                Timer { id: copyFeedback; interval: 1200 }
            }

            ListView {
                id: logListView
                anchors { fill: parent; margins: 8 }
                model: ListModel { id: logModel }
                clip: true; spacing: 1

                delegate: TextEdit {
                    required property string ts
                    required property string msg
                    required property string level
                    width: logListView.width
                    text: ts + " " + msg
                    color: level === "error" ? root.errorRed
                         : level === "warn"  ? root.warnAmber
                         : root.textSecondary
                    font.pixelSize: 11; font.family: "Courier New, monospace"
                    wrapMode: Text.WrapAnywhere; readOnly: true; selectByMouse: true
                    selectedTextColor: root.bgColor; selectionColor: root.textSecondary
                }
            }
        }
    }
}
