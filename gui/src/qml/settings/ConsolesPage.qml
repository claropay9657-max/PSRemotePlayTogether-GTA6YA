import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Your consoles: the registered ones (start right away, login PIN, delete) and the
// ones hidden from the start page, on one page.
C.SettingsPage {
    id: page
    title: qsTr("Your consoles")
    description: qsTr("The consoles this app can play, and the ones you hid from the start page.")

    readonly property var registered: Chiaki.settings.registeredHosts || []
    readonly property var hidden: Chiaki.hiddenHosts || []

    /** What the start page knows of a console right now, by its MAC: where it is, its state and its index there. */
    function seen(mac) {
        const hosts = Chiaki.hosts || [];
        let found = { index: -1, discovered: false, manual: false, state: "unknown", ps5: true };
        for (let i = 0; i < hosts.length; ++i) {
            const h = hosts[i];
            if (!h || h.mac !== mac)
                continue;
            if (h.manual)
                found.manual = true;
            if (h.discovered && !found.discovered) {
                found.index = i;
                found.discovered = true;
                found.state = h.state || "unknown";
                found.ps5 = h.ps5 !== false;
            } else if (found.index < 0) {
                found.index = i;
            }
        }
        return found;
    }

    /** A MAC as people read it: AA:BB:CC:DD:EE:FF. */
    function readableMac(mac) {
        const m = String(mac || "");
        return /^[0-9a-fA-F]{12}$/.test(m) ? m.toUpperCase().match(/.{2}/g).join(":") : m.toUpperCase();
    }

    /** After a row went away: the focus to the row now in its place, or to `fallback`. */
    function refocusAfterRemove(repeater, index, fallback) {
        Qt.callLater(() => {
            const row = repeater.count > 0 ? repeater.itemAt(Math.min(index, repeater.count - 1)) : null;
            let target = row ? row.nextItemInFocusChain(true) : null;
            if (!target || !page.owns(target))
                target = fallback;
            if (target)
                target.forceActiveFocus(Qt.TabFocusReason);
        });
    }

    function owns(item) {
        for (let p = item; p; p = p.parent) {
            if (p === page)
                return true;
        }
        return false;
    }

    /** Another page of the settings window, by its key ("psn", …), as a link: SettingsView.openPage(). */
    function goToPage(key, byKeys) {
        for (let p = page.parent; p; p = p.parent) {
            if (typeof p.openPage === "function") {
                p.openPage(key, "", byKeys);
                return;
            }
        }
    }

    // A console as a small drawing: a PS5 standing up or a PS4 lying flat, its light bar
    // green when it is on, amber in rest mode, grey when it is not seen
    component ConsolePicture: Item {
        id: picture
        property bool ps5: true
        property string consoleState: "unknown"
        property bool seen: false
        readonly property color light: consoleState === "ready" ? C.Theme.good
                                       : consoleState === "standby" ? C.Theme.wait : C.Theme.textDim

        implicitWidth: 30
        implicitHeight: 40

        Rectangle {
            visible: picture.ps5
            x: 7
            y: 3
            width: 16
            height: 34
            radius: 5
            color: picture.seen ? "#2a2448" : "#221d3c"
            border.width: 1
            border.color: picture.seen ? "#4a4173" : "#3a3360"

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: picture.consoleState === "ready" ? 5 : 14
                width: 2
                height: picture.consoleState === "ready" ? 23 : 6
                radius: 1
                color: picture.light
            }
        }

        Rectangle {
            visible: !picture.ps5
            x: 1
            y: 13
            width: 28
            height: 14
            radius: 3
            color: picture.seen ? "#2a2448" : "#221d3c"
            border.width: 1
            border.color: picture.seen ? "#4a4173" : "#3a3360"

            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                x: 6
                width: picture.consoleState === "ready" ? 16 : 6
                height: 2
                radius: 1
                color: picture.light
            }
        }
    }

    // One console in a group: its picture, its name with what it is under it, its controls
    // on the right (under the text where the row is narrow). A row for the search and
    // for the scrolling, as a SettingsRow, with the same thin line between two rows.
    component ConsoleRow: Item {
        id: row
        property string title
        property string subtitle
        property string keywords
        property var badges: []
        property bool ps5: true
        property string consoleState: "unknown"
        property bool seen: false
        property bool dim: false
        default property alias content: controls.data
        readonly property bool isSettingRow: true

        readonly property int leftPad: 16
        readonly property int rightPad: 16
        readonly property int textX: leftPad + 30 + 14
        readonly property bool stacked: width - textX - rightPad < 200 + 20 + controls.implicitWidth
        readonly property bool firstInColumn: {
            const kids = parent ? parent.visibleChildren : [];
            for (let i = 0; i < kids.length; ++i) {
                if (kids[i].isSettingRow === true || kids[i].height > 0)
                    return kids[i] === row;
            }
            return true;
        }
        readonly property real topHeight: Math.max(40, texts.implicitHeight)

        Layout.fillWidth: true
        implicitHeight: (stacked ? topHeight + 10 + controls.implicitHeight
                                 : Math.max(topHeight, controls.implicitHeight)) + 24

        Rectangle {
            visible: !row.firstInColumn
            width: parent.width
            height: 1
            color: Qt.rgba(1, 1, 1, 0.06)
        }

        ConsolePicture {
            x: row.leftPad
            y: row.stacked ? 12 + Math.round((row.topHeight - height) / 2) : Math.round((row.height - height) / 2)
            opacity: row.dim ? 0.6 : 1
            ps5: row.ps5
            consoleState: row.consoleState
            seen: row.seen
        }

        ColumnLayout {
            id: texts
            x: row.textX
            y: row.stacked ? 12 + Math.round((row.topHeight - implicitHeight) / 2) : Math.round((row.height - height) / 2)
            width: row.stacked ? row.width - row.textX - row.rightPad
                               : row.width - row.textX - row.rightPad - 20 - controls.implicitWidth
            spacing: 4

            Text {
                Layout.fillWidth: true
                text: row.title
                wrapMode: Text.WordWrap
                color: row.dim ? "#d9d5f2" : C.Theme.text
                font.pixelSize: 15
                font.weight: Font.ExtraBold
            }

            Flow {
                Layout.fillWidth: true
                visible: row.badges.length > 0
                spacing: 5

                Repeater {
                    model: row.badges

                    C.Badge {
                        required property var modelData
                        small: true
                        text: modelData.text
                        tone: modelData.tone || "neutral"
                    }
                }
            }

            Text {
                Layout.fillWidth: true
                visible: row.badges.length === 0 && text.length > 0
                text: row.subtitle
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                color: C.Theme.textDim
                font.pixelSize: 12
            }
        }

        RowLayout {
            id: controls
            x: row.stacked ? row.textX : row.width - row.rightPad - width
            y: row.stacked ? 12 + row.topHeight + 10 : Math.round((row.height - height) / 2)
            width: row.stacked ? Math.min(implicitWidth, row.width - row.textX - row.rightPad) : implicitWidth
            spacing: 8
        }
    }

    // ------------------------------------------------ register

    GridLayout {
        // Found by the search and kept on screen as a row
        readonly property bool isSettingRow: true
        readonly property string title: qsTr("Register a console")
        readonly property string subtitle: qsTr("Pair this app with your PS5 or PS4 once, with the PIN from the console's Remote Play settings.")
        readonly property string keywords: qsTr("add new pair registration pin ps5 ps4 manual")

        Layout.fillWidth: true
        // The words go under the button where there is little room
        columns: page.width < 560 ? 1 : 2
        columnSpacing: 14
        rowSpacing: 8

        C.PillButton {
            id: registerButton
            kind: "primary"
            iconName: "plus"
            text: qsTr("Register a console")
            onClicked: root.showRegistDialog("255.255.255.255", true)
        }

        Text {
            Layout.fillWidth: true
            text: qsTr("Pair it once with the PIN from the console's Remote Play settings. Consoles on this network also show up on the start page by themselves.")
            wrapMode: Text.WordWrap
            color: C.Theme.textDim
            font.pixelSize: 13
        }
    }

    // ------------------------------------------------ registered

    C.SettingsGroup {
        title: qsTr("Registered")
        note: qsTr("Start right away: plays that console as soon as the app opens")

        C.SettingsRow {
            visible: page.registered.length === 0
            title: qsTr("No console registered yet")
            subtitle: qsTr("Register one above. Logged in to PSN, yours register by themselves.")
            keywords: qsTr("registered consoles")
        }

        Repeater {
            id: registeredRepeater
            model: page.registered

            ConsoleRow {
                id: consoleRow
                required property var modelData
                required property int index
                readonly property var where: page.seen(modelData.mac)
                readonly property string consoleName: modelData.name || qsTr("Console")

                title: consoleName
                keywords: qsTr("registered auto-connect auto connect start right away pin delete remove %1").arg(modelData.ps5 ? "PS5" : "PS4")
                ps5: modelData.ps5 !== false
                consoleState: where.state
                seen: where.discovered
                badges: {
                    let list = [{ text: modelData.ps5 ? "PS5" : "PS4" }];
                    if (where.discovered)
                        list.push({ text: qsTr("On this network"), tone: "good" });
                    if (where.manual)
                        list.push({ text: qsTr("Added by address") });
                    list.push({ text: Chiaki.settings.streamerMode ? qsTr("ID hidden") : page.readableMac(modelData.mac) });
                    return list;
                }
                // For the search: the badges in words
                subtitle: badges.map((badge) => badge.text).join(" · ")
                    + (Chiaki.settings.autoConnectMac === modelData.mac ? " · " + qsTr("starts right away") : "")

                Text {
                    text: qsTr("Start right away")
                    color: "#e7e4fa"
                    font.pixelSize: 13
                    font.weight: Font.Bold

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: {
                            startRightAway.toggle();
                            startRightAway.toggled();
                        }
                    }
                }

                C.Toggle {
                    id: startRightAway
                    Accessible.name: qsTr("Start %1 right away").arg(consoleRow.consoleName)
                    checked: Chiaki.settings.autoConnectMac === consoleRow.modelData.mac
                    // Only one console starts right away: switching one on takes it from the others
                    onToggled: Chiaki.settings.autoConnectMac = checked ? consoleRow.modelData.mac : ""
                }

                // The login PIN goes through the start page's list, so the console has to be there
                Item {
                    implicitWidth: pinButton.implicitWidth
                    implicitHeight: pinButton.implicitHeight
                    Layout.leftMargin: 6
                    QQC.ToolTip.visible: pinHover.hovered && !pinButton.enabled
                    QQC.ToolTip.text: qsTr("Only while the console is on this network or added by its address")
                    QQC.ToolTip.delay: 400

                    HoverHandler {
                        id: pinHover
                    }

                    C.PillButton {
                        id: pinButton
                        anchors.fill: parent
                        small: true
                        text: qsTr("PIN")
                        enabled: consoleRow.where.index >= 0
                        Accessible.name: qsTr("Login PIN for %1").arg(consoleRow.consoleName)
                        onClicked: root.showConsolePinDialog(consoleRow.where.index)
                    }
                }

                C.PillButton {
                    kind: "danger"
                    small: true
                    iconName: "trash"
                    implicitWidth: implicitHeight
                    Accessible.name: qsTr("Delete %1").arg(consoleRow.consoleName)
                    onClicked: {
                        const byKeys = visualFocus;
                        const index = consoleRow.index;
                        root.showConfirmDialog(qsTr("Delete console"),
                            qsTr("Delete %1? To play it again, you register it again.").arg(consoleRow.consoleName),
                            () => {
                                Chiaki.settings.deleteRegisteredHost(index);
                                if (byKeys)
                                    page.refocusAfterRemove(registeredRepeater, index, registerButton);
                            });
                    }
                }
            }
        }
    }

    // ------------------------------------------------ hidden

    C.SettingsGroup {
        title: qsTr("Hidden from the start page")
        note: qsTr("consoles on this network that are not yours")

        C.SettingsRow {
            visible: page.hidden.length === 0
            title: qsTr("None hidden")
            subtitle: qsTr("A console you hide on the start page shows up here, to show it again.")
            keywords: qsTr("hidden consoles unhide show again")
        }

        Repeater {
            id: hiddenRepeater
            model: page.hidden

            ConsoleRow {
                id: hiddenRow
                required property var modelData
                required property int index
                readonly property var where: page.seen(modelData.mac)
                readonly property string consoleName: modelData.name || qsTr("Console")

                title: consoleName
                keywords: qsTr("hidden unhide show again")
                dim: true
                ps5: where.ps5
                consoleState: where.discovered ? where.state : "unknown"
                seen: where.discovered
                subtitle: {
                    let parts = [where.discovered ? qsTr("On this network · not registered") : qsTr("Not registered")];
                    if (!Chiaki.settings.streamerMode)
                        parts.push(page.readableMac(modelData.mac));
                    return parts.join(" · ");
                }

                C.PillButton {
                    small: true
                    text: qsTr("Show again")
                    Accessible.name: qsTr("Show %1 again").arg(hiddenRow.consoleName)
                    onClicked: {
                        const byKeys = visualFocus;
                        const index = hiddenRow.index;
                        Chiaki.unhideHost(hiddenRow.modelData.mac);
                        if (byKeys)
                            page.refocusAfterRemove(hiddenRepeater, index, registerButton);
                    }
                }
            }
        }
    }

    // ------------------------------------------------ PSN

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        C.Icon {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 3
            iconName: "info"
            size: 16
            color: C.Theme.headingSoft
        }

        Flow {
            Layout.fillWidth: true
            spacing: 4

            Text {
                width: Math.min(implicitWidth, parent.width)
                height: Math.max(implicitHeight, 24)
                verticalAlignment: Text.AlignVCenter
                text: qsTr("Logged in to PSN? Your consoles register by themselves. See")
                wrapMode: Text.WordWrap
                color: C.Theme.textSoft
                font.pixelSize: 13
            }

            C.TextLink {
                text: qsTr("Away from home (PSN)")
                onClicked: page.goToPage("psn", visualFocus)
            }
        }
    }
}
