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
    property int    activeTab:     0      // 0=Node  1=Zone
    property bool   settingsOpen:  false
    property bool   pollBusy:      false
    property int    logSeenCount:  0
    property bool   nodeRunning:       false
    property bool   nodeProcessRunning: false   // process alive but HTTP not yet up
    property bool   startedByUs:   false
    property string nodeMode:      ""
    property int    nodeSlot:      0
    property int    nodeLibSlot:   0
    property int    nodeHeight:    0
    property string nodeTip:       ""
    property string nodeLib:       ""
    property string nodeBalance:   "—"

    property var    nodeLogLines:      []
    property string nodeLogFile:       ""
    property var    zoneData:          []
    property string selectedZoneTopic: ""
    property bool   zoneSending:       false
    property string zoneStatus:        ""
    property bool   subscribeOpen:     false

    // ── Helpers ───────────────────────────────────────────────────────────────
    function callModuleParse(raw) {
        try {
            var t = JSON.parse(raw)
            if (typeof t === 'string') { try { return JSON.parse(t) } catch(e) { return t } }
            return t
        } catch(e) { return null }
    }
    function statusColor() {
        if (root.nodeRunning) return root.nodeMode === "Online" ? root.successGreen : root.warnAmber
        if (root.nodeProcessRunning)    return root.warnAmber
        return root.errorRed
    }
    function statusLabel() {
        if (root.nodeRunning) return root.nodeMode === "Online" ? "Online" : (root.nodeMode || "Syncing")
        if (root.nodeProcessRunning)    return "Starting…"
        return "Offline"
    }
    function refreshZone() {
        if (typeof logos === "undefined" || !logos.callModule) return
        var zm = callModuleParse(logos.callModule("logos_node", "getZoneMessages", []))
        if (!zm || !zm.channels) return
        root.zoneData = zm.channels
        rebuildZoneModel()
    }
    function rebuildZoneModel() {
        var channels = root.zoneData
        zoneChannelModel.clear()
        for (var i = 0; i < channels.length; i++) {
            var msgs = channels[i].messages || []
            zoneChannelModel.append({ name: channels[i].channel || "?",
                                      topic: channels[i].topic || "",
                                      msgCount: msgs.length })
        }
        zoneMessageModel.clear()
        for (var i = 0; i < channels.length; i++) {
            var ch = channels[i]
            if (root.selectedZoneTopic !== "" && ch.topic !== root.selectedZoneTopic) continue
            var msgs = ch.messages || []
            for (var j = 0; j < msgs.length; j++) {
                var m = msgs[j]
                zoneMessageModel.append({ channel: ch.channel || "?", msgText: m.text || "",
                                          timestamp: m.timestamp || "",
                                          blockId: (m.block_id || "").substring(0, 12) })
            }
        }
        if (zoneMessageModel.count > 0) zoneListView.positionViewAtEnd()
    }

    TextEdit { id: clipHelper; visible: false }

    // ── 3s poll ───────────────────────────────────────────────────────────────
    Timer {
        interval: 3000; running: true; repeat: true
        onTriggered: {
            if (root.pollBusy) return
            root.pollBusy = true
            var st = root.callModuleParse(logos.callModule("logos_node", "getStatus", []))
            if (st) {
                root.nodeRunning        = st.running === true
                root.nodeProcessRunning = st.processRunning === true
                root.startedByUs        = st.startedByUs === true
                root.nodeMode    = st.mode    || ""
                root.nodeSlot    = st.slot    || 0
                root.nodeLibSlot = st.libSlot || 0
                root.nodeHeight  = st.height  || 0
                root.nodeTip     = st.tip     || ""
                root.nodeLib     = st.lib     || ""
            }
            if (root.nodeRunning) {
                var bal = root.callModuleParse(logos.callModule("logos_node", "getBalance", []))
                root.nodeBalance = (bal && bal.balance !== undefined) ? bal.balance + " LGO" : "—"
            } else { root.nodeBalance = "—" }
            var logArr = root.callModuleParse(logos.callModule("logos_node", "getLog", []))
            if (Array.isArray(logArr) && logArr.length > root.logSeenCount) {
                for (var i = root.logSeenCount; i < logArr.length; i++) {
                    var e = logArr[i]
                    if (logModel.count >= 200) logModel.remove(0)
                    logModel.append({ ts: "[" + (e.ts || "") + "]", msg: e.msg || "", level: e.level || "info" })
                }
                root.logSeenCount = logArr.length
                logListView.positionViewAtEnd()
            }
            if (root.nodeRunning && root.activeTab === 0) {
                var nl = root.callModuleParse(logos.callModule("logos_node", "getNodeLogs", []))
                if (nl && nl.lines) {
                    root.nodeLogFile  = nl.file || ""
                    root.nodeLogLines = nl.lines
                }
            }

            root.pollBusy = false
        }
    }

    Timer {
        interval: 5000; running: true; repeat: true
        onTriggered: { if (!root.pollBusy) root.refreshZone() }
    }


    Component.onCompleted: {
        if (typeof logos === "undefined" || !logos.callModule) return
        var cfg = root.callModuleParse(logos.callModule("logos_node", "getNodeConfig", []))
        if (cfg) {
            binaryField.text   = cfg.binaryPath   || ""
            circuitsField.text = cfg.circuitsPath || ""
            configField.text   = cfg.configPath   || ""
            dataDirField.text  = cfg.dataDir      || ""
        }
        var zcfg = root.callModuleParse(logos.callModule("logos_node", "getZoneConfig", []))
        if (zcfg) {
            walletField.text      = zcfg.walletPubKey        || ""
            zoneBinaryField.text  = zcfg.zoneBoardBinaryPath || ""
            zoneDirField.text     = zcfg.zoneBoardDir        || ""
        }
        root.refreshZone()
    }

    // ── Root layout ───────────────────────────────────────────────────────────
    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ── Toolbar ───────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 12; Layout.leftMargin: 12; Layout.rightMargin: 12
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
                width: 56; height: 24; radius: 4; color: "transparent"
                border.color: root.nodeRunning ? root.errorRed : root.successGreen
                visible: root.nodeRunning ? root.startedByUs : !root.nodeProcessRunning
                Text { anchors.centerIn: parent; text: root.nodeRunning ? "Stop" : "Start"
                       color: root.nodeRunning ? root.errorRed : root.successGreen; font.pixelSize: 11 }
                MouseArea {
                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        if (root.nodeRunning) {
                            logos.callModule("logos_node", "stopNode", [])
                        } else {
                            var r = root.callModuleParse(logos.callModule("logos_node", "startNode", []))
                            if (r && r.error) logModel.append({ ts: "[now]", msg: "start error: " + r.error, level: "error" })
                        }
                    }
                }
            }
            Rectangle {
                width: 24; height: 24; radius: 4; color: "transparent"
                border.color: root.settingsOpen ? root.accentBlue : root.borderColor
                Text { anchors.centerIn: parent; text: "⚙"
                       color: root.settingsOpen ? root.accentBlue : root.textSecondary; font.pixelSize: 12 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.settingsOpen = !root.settingsOpen }
            }
        }

        // ── Tab bar ───────────────────────────────────────────────────────────
        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: 10; Layout.leftMargin: 12; Layout.rightMargin: 12
            spacing: 0

            Repeater {
                model: ["Node", "Zone"]
                delegate: Item {
                    required property string modelData
                    required property int    index
                    Layout.fillWidth: true; height: 28

                    Text {
                        anchors.centerIn: parent
                        text: modelData
                        color: root.activeTab === index ? root.textPrimary : root.textDisabled
                        font.pixelSize: 12; font.bold: root.activeTab === index
                    }
                    Rectangle {
                        anchors { bottom: parent.bottom; left: parent.left; right: parent.right }
                        height: 2; radius: 1
                        color: root.activeTab === index ? root.accentBlue : root.borderColor
                    }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.activeTab = index }
                }
            }
        }

        // ── Tab content ───────────────────────────────────────────────────────
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8

            // ── NODE TAB ──────────────────────────────────────────────────────
            ColumnLayout {
                visible: root.activeTab === 0
                anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                spacing: 8

                // Sync bar
                Rectangle {
                    Layout.fillWidth: true; height: 3; radius: 2; color: root.borderColor
                    visible: root.nodeRunning && root.nodeMode !== "Online" && root.nodeSlot > 0
                    Rectangle {
                        height: parent.height; radius: parent.radius; color: root.warnAmber
                        width: root.nodeSlot > 0 ? Math.min(1.0, root.nodeLibSlot / root.nodeSlot) * parent.width : 0
                    }
                }

                // Stat cards
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    visible: root.nodeRunning

                    Repeater {
                        model: [
                            { label: "Mode",     val: root.nodeMode || "—",
                              clr: root.nodeMode === "Online" ? root.successGreen : root.nodeMode.length > 0 ? root.warnAmber : root.textDisabled },
                            { label: "Height",   val: root.nodeHeight  > 0 ? root.nodeHeight.toLocaleString(Qt.locale(),'f',0)  : "—", clr: root.accentBlue },
                            { label: "Slot",     val: root.nodeSlot    > 0 ? root.nodeSlot.toLocaleString(Qt.locale(),'f',0)    : "—", clr: root.textPrimary },
                            { label: "LIB Slot", val: root.nodeLibSlot > 0 ? root.nodeLibSlot.toLocaleString(Qt.locale(),'f',0) : "—", clr: root.textSecondary },
                            { label: "Balance",  val: root.nodeBalance,
                              clr: root.nodeBalance === "—" ? root.textDisabled : root.successGreen }
                        ]
                        delegate: Rectangle {
                            required property var modelData
                            Layout.fillWidth: true; height: 56
                            color: root.panelColor; border.color: root.borderColor; radius: 4
                            ColumnLayout {
                                anchors.centerIn: parent; spacing: 2
                                Text { text: modelData.label; color: root.textSecondary; font.pixelSize: 9; Layout.alignment: Qt.AlignHCenter }
                                Text { text: modelData.val; color: modelData.clr
                                       font.pixelSize: modelData.val.length > 8 ? 10 : 13; font.bold: true
                                       Layout.alignment: Qt.AlignHCenter }
                            }
                        }
                    }
                }

                // Chain view
                Rectangle {
                    Layout.fillWidth: true
                    color: root.panelColor; border.color: root.borderColor; radius: 4
                    height: chainInner.implicitHeight + 14
                    visible: root.nodeTip.length > 0

                    ColumnLayout {
                        id: chainInner
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 8 }
                        spacing: 4
                        RowLayout { spacing: 6
                            Text { text: "Tip"; color: root.textDisabled; font.pixelSize: 10; font.family: "monospace" }
                            Text { text: root.nodeTip.length > 16 ? root.nodeTip.substring(0, 16) + "…" : root.nodeTip
                                   color: root.textPrimary; font.pixelSize: 11; font.family: "monospace" } }
                        RowLayout { spacing: 6
                            Text { text: "LIB"; color: root.textDisabled; font.pixelSize: 10; font.family: "monospace" }
                            Text { text: root.nodeLib.length > 16 ? root.nodeLib.substring(0, 16) + "…" : root.nodeLib
                                   color: root.textSecondary; font.pixelSize: 11; font.family: "monospace" } }
                    }
                }

                // Settings panel
                Rectangle {
                    Layout.fillWidth: true
                    color: root.panelColor; border.color: root.borderColor; radius: 4
                    visible: root.settingsOpen
                    height: visible ? sInner.implicitHeight + 20 : 0

                    ColumnLayout {
                        id: sInner
                        anchors { left: parent.left; right: parent.right; top: parent.top; margins: 10 }
                        spacing: 8

                        Text { text: "Node Binary"; color: root.textSecondary; font.pixelSize: 10 }
                        Rectangle { Layout.fillWidth: true; height: 26; color: "#0A0A0A"; border.color: root.borderColor; radius: 3
                            TextInput { id: binaryField; anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter; color: root.textPrimary; font.pixelSize: 11; font.family: "monospace"; clip: true
                                Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter; text: parent.text.length === 0 ? "/path/to/logos-blockchain-node" : ""; color: root.textDisabled; font.pixelSize: 11; font.family: "monospace" } } }

                        Text { text: "Circuits Dir"; color: root.textSecondary; font.pixelSize: 10 }
                        Rectangle { Layout.fillWidth: true; height: 26; color: "#0A0A0A"; border.color: root.borderColor; radius: 3
                            TextInput { id: circuitsField; anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter; color: root.textPrimary; font.pixelSize: 11; font.family: "monospace"; clip: true
                                Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter; text: parent.text.length === 0 ? "/path/to/circuits-vX-linux-x86_64" : ""; color: root.textDisabled; font.pixelSize: 11; font.family: "monospace" } } }

                        Text { text: "Config File"; color: root.textSecondary; font.pixelSize: 10 }
                        Rectangle { Layout.fillWidth: true; height: 26; color: "#0A0A0A"; border.color: root.borderColor; radius: 3
                            TextInput { id: configField; anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter; color: root.textPrimary; font.pixelSize: 11; font.family: "monospace"; clip: true
                                Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter; text: parent.text.length === 0 ? "/path/to/node.yaml" : ""; color: root.textDisabled; font.pixelSize: 11; font.family: "monospace" } } }

                        Text { text: "Data Dir"; color: root.textSecondary; font.pixelSize: 10 }
                        Rectangle { Layout.fillWidth: true; height: 26; color: "#0A0A0A"; border.color: root.borderColor; radius: 3
                            TextInput { id: dataDirField; anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter; color: root.textPrimary; font.pixelSize: 11; font.family: "monospace"; clip: true
                                Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter; text: parent.text.length === 0 ? "/path/to/state/live" : ""; color: root.textDisabled; font.pixelSize: 11; font.family: "monospace" } } }

                        Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor; opacity: 0.5 }

                        Text { text: "Wallet Public Key"; color: root.textSecondary; font.pixelSize: 10 }
                        Rectangle { Layout.fillWidth: true; height: 26; color: "#0A0A0A"; border.color: root.borderColor; radius: 3
                            TextInput { id: walletField; anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter; color: root.textPrimary; font.pixelSize: 11; font.family: "monospace"; clip: true
                                Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter; text: parent.text.length === 0 ? "hex public key for balance" : ""; color: root.textDisabled; font.pixelSize: 11; font.family: "monospace" } } }

                        Text { text: "Zone Board Binary"; color: root.textSecondary; font.pixelSize: 10 }
                        Rectangle { Layout.fillWidth: true; height: 26; color: "#0A0A0A"; border.color: root.borderColor; radius: 3
                            TextInput { id: zoneBinaryField; anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter; color: root.textPrimary; font.pixelSize: 11; font.family: "monospace"; clip: true
                                Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter; text: parent.text.length === 0 ? "/path/to/zone-board" : ""; color: root.textDisabled; font.pixelSize: 11; font.family: "monospace" } } }

                        Text { text: "Zone Board Dir"; color: root.textSecondary; font.pixelSize: 10 }
                        Rectangle { Layout.fillWidth: true; height: 26; color: "#0A0A0A"; border.color: root.borderColor; radius: 3
                            TextInput { id: zoneDirField; anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                                verticalAlignment: TextInput.AlignVCenter; color: root.textPrimary; font.pixelSize: 11; font.family: "monospace"; clip: true
                                Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter; text: parent.text.length === 0 ? "/path/to/zone-board-vX" : ""; color: root.textDisabled; font.pixelSize: 11; font.family: "monospace" } } }

                        RowLayout {
                            Layout.fillWidth: true
                            Item { Layout.fillWidth: true }
                            Rectangle {
                                width: 86; height: 24; radius: 4; color: "transparent"; border.color: root.successGreen
                                Text { anchors.centerIn: parent; text: "Save All"; color: root.successGreen; font.pixelSize: 11 }
                                MouseArea {
                                    anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: {
                                        logos.callModule("logos_node", "setNodeConfig", [binaryField.text, circuitsField.text, configField.text, dataDirField.text])
                                        logos.callModule("logos_node", "setZoneConfig", [walletField.text, zoneBinaryField.text, zoneDirField.text])
                                        root.refreshZone(); root.settingsOpen = false
                                    }
                                }
                            }
                        }
                    }
                }

                // Live blockchain logs
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#080808"; border.color: root.borderColor; radius: 3
                    visible: root.nodeRunning

                    Text {
                        id: nodeLogLabel
                        anchors { top: parent.top; left: parent.left; margins: 6 }
                        text: root.nodeLogFile || "blockchain logs"
                        color: root.textDisabled; font.pixelSize: 9; font.family: "monospace"
                    }

                    ListView {
                        id: nodeLogView
                        anchors { fill: parent; topMargin: 18; margins: 4 }
                        model: root.nodeLogLines
                        clip: true
                        spacing: 0
                        onCountChanged: positionViewAtEnd()

                        delegate: Text {
                            required property string modelData
                            width: nodeLogView.width
                            text: modelData
                            color: modelData.indexOf("ERROR") >= 0 ? root.errorRed
                                 : modelData.indexOf("WARN")  >= 0 ? root.warnAmber
                                 : modelData.indexOf("DEBUG") >= 0 ? root.textDisabled
                                 : root.textSecondary
                            font.pixelSize: 10; font.family: "Courier New, monospace"
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                }

                Item { Layout.fillHeight: true; visible: !root.nodeRunning }
            }

            // ── ZONE TAB ──────────────────────────────────────────────────────
            ColumnLayout {
                visible: root.activeTab === 1
                anchors { fill: parent; leftMargin: 12; rightMargin: 12 }
                spacing: 6

                // Channel tabs
                RowLayout {
                    Layout.fillWidth: true; spacing: 6

                    Flickable {
                        Layout.fillWidth: true; height: 26
                        contentWidth: zoneTabRow.width; clip: true
                        flickableDirection: Flickable.HorizontalFlick

                        Row {
                            id: zoneTabRow; spacing: 4

                            Rectangle {
                                height: 22; width: allTabTxt.width + 16; radius: 11
                                color: root.selectedZoneTopic === "" ? Qt.rgba(56/255,189/255,248/255,0.12) : "transparent"
                                border.color: root.selectedZoneTopic === "" ? root.accentBlue : root.borderColor
                                Text { id: allTabTxt; anchors.centerIn: parent; text: "All"
                                       color: root.selectedZoneTopic === "" ? root.textPrimary : root.textSecondary; font.pixelSize: 10 }
                                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                    onClicked: { root.selectedZoneTopic = ""; root.rebuildZoneModel() } }
                            }

                            Repeater {
                                model: ListModel { id: zoneChannelModel }
                                delegate: Rectangle {
                                    required property string name
                                    required property string topic
                                    required property int    msgCount
                                    height: 22; width: tabLbl.width + 16; radius: 11
                                    color: root.selectedZoneTopic === topic ? Qt.rgba(56/255,189/255,248/255,0.12) : "transparent"
                                    border.color: root.selectedZoneTopic === topic ? root.accentBlue : root.borderColor
                                    Text { id: tabLbl; anchors.centerIn: parent
                                           text: name + (msgCount > 0 ? " (" + msgCount + ")" : "")
                                           color: root.selectedZoneTopic === topic ? root.textPrimary : root.textSecondary; font.pixelSize: 10 }
                                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                                        onClicked: { root.selectedZoneTopic = (root.selectedZoneTopic === topic) ? "" : topic; root.rebuildZoneModel() } }
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: 72; height: 22; radius: 11; color: "transparent"
                        border.color: root.subscribeOpen ? root.accentBlue : root.borderColor
                        Text { anchors.centerIn: parent; text: "+ Subscribe"
                               color: root.subscribeOpen ? root.accentBlue : root.textDisabled; font.pixelSize: 10 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.subscribeOpen = !root.subscribeOpen }
                    }
                }

                // Message list — fillHeight, internal scroll
                Rectangle {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    color: "#080808"; border.color: root.borderColor; radius: 3

                    Text {
                        anchors.centerIn: parent
                        visible: zoneMessageModel.count === 0
                        text: "No messages yet"; color: root.textDisabled; font.pixelSize: 11
                    }

                    ListView {
                        id: zoneListView
                        anchors { fill: parent; margins: 6 }
                        model: ListModel { id: zoneMessageModel }
                        clip: true; spacing: 6

                        delegate: Rectangle {
                            required property string channel
                            required property string msgText
                            required property string timestamp
                            required property string blockId
                            width: zoneListView.width
                            height: msgCol.implicitHeight + 8
                            color: "transparent"

                            ColumnLayout {
                                id: msgCol
                                anchors { left: parent.left; right: parent.right; top: parent.top; topMargin: 4 }
                                spacing: 2

                                RowLayout { spacing: 6
                                    Rectangle { width: chLbl.width + 8; height: 14; radius: 7; color: root.borderColor
                                        Text { id: chLbl; anchors.centerIn: parent; text: channel; color: root.textSecondary; font.pixelSize: 9 } }
                                    Text { text: timestamp; color: root.textDisabled; font.pixelSize: 9 }
                                    Text { visible: blockId.length > 0; text: blockId + "…"; color: root.textDisabled; font.pixelSize: 9; font.family: "monospace" }
                                }
                                Text { Layout.fillWidth: true; text: msgText; color: root.textPrimary; font.pixelSize: 12; wrapMode: Text.WrapAnywhere }
                                Rectangle { Layout.fillWidth: true; height: 1; color: root.borderColor; opacity: 0.4 }
                            }
                        }
                    }
                }

                // Subscribe input
                RowLayout {
                    Layout.fillWidth: true; visible: root.subscribeOpen; spacing: 6
                    Rectangle { Layout.fillWidth: true; height: 26; color: "#0A0A0A"; border.color: root.borderColor; radius: 3
                        TextInput { id: subscribeField; anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                            verticalAlignment: TextInput.AlignVCenter; color: root.textPrimary; font.pixelSize: 11; clip: true
                            Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter; text: parent.text.length === 0 ? "channel name (e.g. vpavlin)" : ""; color: root.textDisabled; font.pixelSize: 11 } } }
                    Rectangle { width: 40; height: 26; radius: 3; color: "transparent"; border.color: root.accentBlue
                        Text { anchors.centerIn: parent; text: "Sub"; color: root.accentBlue; font.pixelSize: 11 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (!subscribeField.text.trim()) return
                                var r = root.callModuleParse(logos.callModule("logos_node", "subscribeZoneChannel", [subscribeField.text.trim()]))
                                root.zoneStatus = r && r.ok ? "Subscribed — syncing…" : (r && r.error ? r.error : "failed")
                                subscribeField.text = ""; root.subscribeOpen = false; root.refreshZone()
                            } } }
                    Rectangle { width: 26; height: 26; radius: 3; color: "transparent"; border.color: root.borderColor
                        Text { anchors.centerIn: parent; text: "✕"; color: root.textDisabled; font.pixelSize: 11 }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { root.subscribeOpen = false; subscribeField.text = "" } } }
                }

                // Compose
                RowLayout {
                    Layout.fillWidth: true; spacing: 6
                    Rectangle { Layout.fillWidth: true; height: 26; color: "#0A0A0A"; border.color: root.borderColor; radius: 3
                        TextInput { id: zoneComposeField; anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                            verticalAlignment: TextInput.AlignVCenter; color: root.textPrimary; font.pixelSize: 11; clip: true; enabled: !root.zoneSending
                            Text { anchors.fill: parent; verticalAlignment: Text.AlignVCenter; text: parent.text.length === 0 ? "Publish to your channel…" : ""; color: root.textDisabled; font.pixelSize: 11 } } }
                    Rectangle { width: 44; height: 26; radius: 3
                        color: root.zoneSending ? "transparent" : "#0284c7"
                        border.color: root.zoneSending ? root.borderColor : "#0ea5e9"
                        opacity: root.zoneSending ? 0.6 : 1.0
                        Text { anchors.centerIn: parent; text: root.zoneSending ? "…" : "Send"; color: "#f8fafc"; font.pixelSize: 11; font.bold: true }
                        MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; enabled: !root.zoneSending
                            onClicked: {
                                var msg = zoneComposeField.text.trim()
                                if (!msg) return
                                root.zoneSending = true; root.zoneStatus = "Sending…"
                                var r = root.callModuleParse(logos.callModule("logos_node", "publishZoneMessage", [msg]))
                                if (r && r.ok) { zoneComposeField.text = ""; root.zoneStatus = "Sent — waiting for finalization…"; zoneRefreshDelay.restart() }
                                else { root.zoneStatus = r && r.error ? r.error : "send failed" }
                                root.zoneSending = false
                            } } }
                }

                Timer { id: zoneRefreshDelay; interval: 1500; onTriggered: root.refreshZone() }

                Text { visible: root.zoneStatus.length > 0; text: root.zoneStatus; color: root.textDisabled
                       font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WrapAnywhere }

                Item { height: 4 }
            }
        }

        // ── Activity log — fixed height, always visible ───────────────────────
        Rectangle {
            Layout.fillWidth: true
            height: 120
            color: "#0D0D0D"

            Rectangle {
                anchors { top: parent.top; left: parent.left; right: parent.right }
                height: 1; color: root.borderColor
            }

            Rectangle {
                z: 10; anchors { top: parent.top; right: parent.right; topMargin: 4; rightMargin: 6 }
                width: 22; height: 22; radius: 3; color: "transparent"; border.color: root.borderColor
                visible: logModel.count > 0
                Text { anchors.centerIn: parent; text: copyFeedback.running ? "✓" : "⎘"
                       color: copyFeedback.running ? root.successGreen : root.textDisabled; font.pixelSize: 11 }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        var text = ""
                        for (var i = 0; i < logModel.count; i++) { var e = logModel.get(i); text += e.ts + " " + e.msg + "\n" }
                        clipHelper.text = text; clipHelper.selectAll(); clipHelper.copy(); copyFeedback.restart()
                    } }
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
                    color: level === "error" ? root.errorRed : level === "warn" ? root.warnAmber : root.textSecondary
                    font.pixelSize: 11; font.family: "Courier New, monospace"
                    wrapMode: Text.WrapAnywhere; readOnly: true; selectByMouse: true
                    selectedTextColor: root.bgColor; selectionColor: root.textSecondary
                }
            }
        }

    } // ColumnLayout
}
