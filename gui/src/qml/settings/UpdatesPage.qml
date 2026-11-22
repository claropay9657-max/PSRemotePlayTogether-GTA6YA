import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Updates & logs: which version this is and whether a newer one is out, updating to it
// from here (Chiaki.updater, appupdater.h), what is new in each version, the update check,
// logs for bug reports, and the credits and licenses.
C.SettingsPage {
    id: page
    title: qsTr("Updates & logs")
    description: qsTr("Which version you have, newer ones, and logs for bug reports.")

    /** A newer release than this build, found by the update check; "" when none is known. */
    readonly property string newer: Chiaki.updateVersion || ""
    /** The app updating itself, and the changelog. */
    readonly property var up: Chiaki.updater
    /** The versions whose changes are open on this page; this one's from the start. */
    property var opened: ({})
    /** The newest versions are listed from the start, the ones before them once asked for: the page stays short. */
    readonly property int firstVersions: 4
    property bool allVersions: false

    function isOpen(version) {
        return opened[version] !== undefined ? opened[version] : version === Qt.application.version;
    }

    function setOpen(version, open) {
        const next = Object.assign({}, opened);
        next[version] = open;
        opened = next;
    }
    /** "Check now" was pressed and the answer may still come. */
    property bool checking: false
    property bool checkedNow: false

    function checkNow() {
        // Not switched off meanwhile: that would take the focus away from it
        if (checking)
            return;
        checkedNow = false;
        checking = true;
        Chiaki.checkForUpdate();
        checkTimer.restart();
    }

    // The update check answers within 10 s, or not at all (offline): it only ever says when there is something newer
    Timer {
        id: checkTimer
        interval: 6000
        onTriggered: {
            page.checking = false;
            page.checkedNow = true;
        }
    }

    /**
     * What changed, as a list: each entry with what it is in bold and the rest after it. What
     * is new has a mark that says so, the note at the end an "i".
     */
    component ChangeList: ColumnLayout {
        id: changeList
        /** [{ html, isNew, isNote }, …] as Chiaki.updater has them. */
        property var changes: []

        spacing: 10

        Repeater {
            model: changeList.changes

            RowLayout {
                id: change
                required property var modelData

                Layout.fillWidth: true
                spacing: 12

                // What kind of entry it is, in a column of its own so that the texts line up
                Item {
                    Layout.alignment: Qt.AlignTop
                    Layout.preferredWidth: 40
                    Layout.preferredHeight: 20

                    Rectangle {
                        visible: change.modelData.isNew
                        anchors.right: parent.right
                        y: 1
                        width: newWord.implicitWidth + 12
                        height: 18
                        radius: 9
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop { position: 0.0; color: C.Theme.buttonFrom }
                            GradientStop { position: 1.0; color: C.Theme.buttonTo }
                        }

                        Text {
                            id: newWord
                            anchors.centerIn: parent
                            text: qsTr("NEW")
                            color: "#ffffff"
                            font.pixelSize: 10
                            font.weight: Font.ExtraBold
                            font.letterSpacing: 0.6
                        }
                    }

                    C.Icon {
                        visible: change.modelData.isNote
                        anchors.right: parent.right
                        anchors.rightMargin: 2
                        y: 1
                        iconName: "info"
                        size: 18
                        color: C.Theme.wait
                    }

                    Rectangle {
                        visible: !change.modelData.isNew && !change.modelData.isNote
                        anchors.right: parent.right
                        anchors.rightMargin: 7
                        y: 7
                        width: 7
                        height: 7
                        radius: 3.5
                        color: "#a77bff"
                    }
                }

                Text {
                    Layout.fillWidth: true
                    textFormat: Text.RichText
                    wrapMode: Text.WordWrap
                    lineHeight: 1.2
                    color: change.modelData.isNote ? C.Theme.textDim : C.Theme.textSoft
                    font.pixelSize: 14
                    // What it is stands out; names of commands look like what is typed
                    text: change.modelData.html
                          .replace(/<b>/g, "<span style=\"color:#f3f1ff; font-weight:800\">").replace(/<\/b>/g, "</span>")
                          .replace(/<code>/g, "<span style=\"font-family:Menlo,Consolas,'DejaVu Sans Mono',monospace; color:#cdb6ff\">").replace(/<\/code>/g, "</span>")
                }
            }
        }
    }

    // Ko-fi's own coral, as on the start page
    component KofiButton: QQC.AbstractButton {
        id: kofi
        property bool firstInFocusChain: false
        property bool lastInFocusChain: false

        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        implicitHeight: 34
        implicitWidth: kofiContent.implicitWidth + 28
        Accessible.name: qsTr("Support the project on Ko-fi")
        Keys.onPressed: (event) => C.FocusChain.handle(kofi, event)

        background: Item {
            Rectangle {
                anchors.fill: parent
                radius: height / 2
                color: Qt.rgba(1, 94 / 255, 91 / 255, kofi.down ? 0.22 : kofi.hovered ? 0.16 : 0.1)
                border.width: 1
                border.color: Qt.rgba(1, 94 / 255, 91 / 255, 0.6)
            }

            Rectangle {
                anchors.fill: parent
                anchors.margins: -4
                radius: height / 2
                color: "transparent"
                border.width: 2
                border.color: C.Theme.focus
                visible: kofi.visualFocus
            }
        }

        contentItem: Item {
            implicitWidth: kofiContent.implicitWidth
            implicitHeight: kofiContent.implicitHeight

            Row {
                id: kofiContent
                anchors.centerIn: parent
                spacing: 7

                C.Icon {
                    anchors.verticalCenter: parent.verticalCenter
                    iconName: "heart"
                    size: 14
                    color: "#ffb3b1"
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "Ko-fi"
                    color: "#ffb3b1"
                    font.pixelSize: 13
                    font.weight: Font.ExtraBold
                }
            }
        }

        onClicked: Qt.openUrlExternally("https://ko-fi.com/crypto90")
    }

    // ------------------------------------------------ this version

    Rectangle {
        id: about
        // Found by the search and kept on screen as a row
        readonly property bool isSettingRow: true
        readonly property string title: "PSRemotePlayTogether"
        readonly property string subtitle: page.newer ? qsTr("Version %1 is out").arg(page.newer) : qsTr("Version %1").arg(Qt.application.version)
        readonly property string keywords: qsTr("about version update new release download page gamernova chiaki-ng")
        readonly property bool narrow: width < 660

        Layout.fillWidth: true
        implicitHeight: aboutGrid.implicitHeight + 40
        radius: 20
        color: C.Theme.card
        border.width: 1
        border.color: C.Theme.line

        GridLayout {
            id: aboutGrid
            x: 22
            y: 20
            width: parent.width - 44
            columns: about.narrow ? 2 : 3
            columnSpacing: 18
            rowSpacing: 14

            // The app's own icon (qrc:/icons/ui/app-icon.svg)
            Image {
                Layout.preferredWidth: 60
                Layout.preferredHeight: 60
                Layout.alignment: Qt.AlignVCenter
                source: C.Theme.iconBase + "app-icon.svg"
                sourceSize: Qt.size(120, 120)
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 3

                C.GradientText {
                    text: "PSRemotePlayTogether"
                    font.pixelSize: 19
                    font.weight: Font.ExtraBold
                    from: "#a77bff"
                    to: C.Theme.brandTo
                }

                Text {
                    Layout.fillWidth: true
                    text: qsTr("Version %1").arg(Qt.application.version)
                    wrapMode: Text.WordWrap
                    color: "#e7e4fa"
                    font.pixelSize: 13
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        height: nova.height
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("powered by")
                        color: C.Theme.textDim
                        font.pixelSize: 12
                    }

                    C.TextLink {
                        id: nova
                        text: "GamerNova.net"
                        font.pixelSize: 12
                        Accessible.name: qsTr("GamerNova.net, opens in your browser")
                        onClicked: Qt.openUrlExternally("https://gamernova.net/")
                    }

                    Text {
                        height: nova.height
                        verticalAlignment: Text.AlignVCenter
                        text: qsTr("· built on chiaki-ng")
                        color: C.Theme.textDim
                        font.pixelSize: 12
                    }
                }
            }

            // A newer version, or a way to look for one
            ColumnLayout {
                Layout.columnSpan: about.narrow ? 2 : 1
                // Under the words where it is narrow, not under the icon
                Layout.leftMargin: about.narrow ? 78 : 0
                Layout.alignment: about.narrow ? Qt.AlignLeft | Qt.AlignVCenter : Qt.AlignRight | Qt.AlignVCenter
                spacing: 8

                C.Badge {
                    visible: page.newer.length > 0
                    Layout.alignment: about.narrow ? Qt.AlignLeft : Qt.AlignRight
                    tone: "play"
                    dot: true
                    text: qsTr("%1 is out").arg(page.newer)
                }

                // The app puts the new version in place itself, where it can
                C.PillButton {
                    visible: page.newer.length > 0 && page.up.possible && !page.up.busy
                    Layout.alignment: about.narrow ? Qt.AlignLeft : Qt.AlignRight
                    implicitHeight: 40
                    kind: "primary"
                    iconName: "download"
                    // Not in the middle of a game
                    enabled: !Chiaki.session
                    text: page.up.state === "failed" ? qsTr("Try again") : qsTr("Update and restart")
                    Accessible.description: qsTr("Downloads version %1, checks it, puts it in place and starts the app again.").arg(page.newer)
                    onClicked: page.up.start()
                }

                // At work: how far, and a way to stop while it still only downloads
                ColumnLayout {
                    visible: page.newer.length > 0 && page.up.busy
                    Layout.alignment: about.narrow ? Qt.AlignLeft : Qt.AlignRight
                    // As wide as this, or as the card has room for
                    Layout.fillWidth: true
                    Layout.preferredWidth: 260
                    Layout.maximumWidth: 260
                    spacing: 6

                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: about.narrow ? Text.AlignLeft : Text.AlignRight
                        wrapMode: Text.WordWrap
                        color: C.Theme.text
                        font.pixelSize: 13
                        font.weight: Font.Bold
                        text: page.up.state === "download" ? qsTr("Downloading version %1… %2 %").arg(page.newer).arg(Math.round(page.up.progress * 100))
                              : page.up.state === "check" ? qsTr("Checking the download…")
                              : page.up.state === "install" ? qsTr("Putting version %1 in place…").arg(page.newer)
                              : qsTr("Starting version %1…").arg(page.newer)
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 6
                        radius: 3
                        color: Qt.rgba(1, 1, 1, 0.14)

                        Rectangle {
                            width: parent.width * (page.up.state === "download" ? page.up.progress : 1)
                            height: parent.height
                            radius: 3
                            gradient: Gradient {
                                orientation: Gradient.Horizontal
                                GradientStop { position: 0.0; color: C.Theme.brandFrom }
                                GradientStop { position: 1.0; color: C.Theme.brandTo }
                            }
                        }
                    }

                    C.PillButton {
                        visible: page.up.state === "download"
                        Layout.alignment: about.narrow ? Qt.AlignLeft : Qt.AlignRight
                        small: true
                        text: qsTr("Stop")
                        onClicked: page.up.cancel()
                    }
                }

                // What went wrong, or why the app cannot replace itself
                Text {
                    visible: page.newer.length > 0 && !page.up.busy && text.length > 0
                    Layout.alignment: about.narrow ? Qt.AlignLeft : Qt.AlignRight
                    Layout.fillWidth: true
                    Layout.preferredWidth: 300
                    Layout.maximumWidth: 300
                    horizontalAlignment: about.narrow ? Text.AlignLeft : Text.AlignRight
                    wrapMode: Text.WordWrap
                    lineHeight: 1.15
                    color: page.up.state === "failed" ? "#ffc2d1" : C.Theme.textDim
                    font.pixelSize: 12
                    text: page.up.state === "failed" ? page.up.error
                          : !page.up.possible ? page.up.reason
                          : Chiaki.session ? qsTr("After the game: an update closes the app.") : ""
                }

                // The download page: the way where the app cannot do it, and always there
                C.PillButton {
                    visible: page.newer.length > 0 && !page.up.busy
                    Layout.alignment: about.narrow ? Qt.AlignLeft : Qt.AlignRight
                    implicitHeight: page.up.possible ? 34 : 40
                    small: page.up.possible
                    kind: page.up.possible ? "ghost" : "primary"
                    iconName: "external"
                    text: qsTr("Open the download page")
                    onClicked: Chiaki.openUpdatePage()
                }

                // The check only runs while "Check for updates" is on
                C.PillButton {
                    visible: page.newer.length === 0 && Chiaki.settings.updateCheckEnabled
                    Layout.alignment: about.narrow ? Qt.AlignLeft : Qt.AlignRight
                    small: true
                    iconName: "refresh"
                    text: page.checking ? qsTr("Checking…") : qsTr("Check now")
                    onClicked: page.checkNow()
                }

                Text {
                    visible: page.checkedNow && page.newer.length === 0 && Chiaki.settings.updateCheckEnabled
                    Layout.alignment: about.narrow ? Qt.AlignLeft : Qt.AlignRight
                    text: qsTr("No newer version found")
                    color: C.Theme.textDim
                    font.pixelSize: 12
                }
            }
        }
    }

    // ------------------------------------------------ what is new

    // In the version that is out, before it is installed
    Rectangle {
        id: comingCard
        readonly property bool isSettingRow: true
        readonly property string title: qsTr("New in version %1").arg(page.newer)
        readonly property string subtitle: qsTr("What the update brings.")
        readonly property string keywords: qsTr("changelog changes release notes what is new whats new")

        visible: page.newer.length > 0 && page.up.news.length > 0
        Layout.fillWidth: true
        implicitHeight: comingColumn.implicitHeight + 36
        radius: 20
        color: C.Theme.card
        border.width: 1
        border.color: Qt.rgba(140 / 255, 82 / 255, 1, 0.45)

        ColumnLayout {
            id: comingColumn
            x: 20
            y: 18
            width: parent.width - 40
            spacing: 14

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                C.GradientText {
                    text: comingCard.title
                    font.pixelSize: 17
                    font.weight: Font.ExtraBold
                    from: "#a77bff"
                    to: C.Theme.brandTo
                }

                C.Badge {
                    tone: "play"
                    dot: true
                    text: qsTr("out now")
                }

                Item {
                    Layout.fillWidth: true
                }
            }

            ChangeList {
                Layout.fillWidth: true
                changes: page.up.news
            }
        }
    }

    // In this version and the ones before it: the app carries its changelog
    Rectangle {
        id: historyCard
        readonly property bool isSettingRow: true
        readonly property string title: qsTr("What's new")
        readonly property string subtitle: qsTr("What changed in this version and in the ones before it.")
        readonly property string keywords: qsTr("changelog changes release notes history versions whats new")

        visible: page.up.history.length > 0
        Layout.fillWidth: true
        implicitHeight: historyColumn.implicitHeight + 32
        radius: 20
        color: C.Theme.card
        border.width: 1
        border.color: C.Theme.line

        ColumnLayout {
            id: historyColumn
            x: 20
            y: 16
            width: parent.width - 40
            spacing: 4

            C.SectionLabel {
                Layout.bottomMargin: 6
                text: historyCard.title
            }

            Repeater {
                model: page.up.history

                ColumnLayout {
                    id: entry
                    required property var modelData
                    required property int index
                    readonly property bool open: page.isOpen(modelData.version)
                    readonly property bool mine: modelData.version === Qt.application.version

                    visible: page.allVersions || index < page.firstVersions
                    Layout.fillWidth: true
                    spacing: 0

                    Rectangle {
                        visible: entry.index > 0
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: Qt.rgba(1, 1, 1, 0.06)
                    }

                    // The version: a click opens and closes what changed in it
                    QQC.AbstractButton {
                        id: versionButton
                        property bool firstInFocusChain: false
                        property bool lastInFocusChain: false

                        Layout.fillWidth: true
                        implicitHeight: 44
                        hoverEnabled: true
                        focusPolicy: Qt.StrongFocus
                        Accessible.name: entry.open ? qsTr("Version %1, close").arg(entry.modelData.version)
                                                    : qsTr("Version %1, show what changed").arg(entry.modelData.version)
                        onClicked: page.setOpen(entry.modelData.version, !entry.open)
                        Keys.onPressed: (event) => C.FocusChain.handle(versionButton, event)

                        background: Rectangle {
                            radius: 10
                            color: Qt.rgba(1, 1, 1, versionButton.down ? 0.07 : versionButton.hovered ? 0.04 : 0)

                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -2
                                radius: 12
                                color: "transparent"
                                border.width: 2
                                border.color: C.Theme.focus
                                visible: versionButton.visualFocus
                            }
                        }

                        contentItem: RowLayout {
                            spacing: 10

                            C.Icon {
                                Layout.leftMargin: 6
                                iconName: "chevron-right"
                                size: 16
                                color: C.Theme.textSoft
                                rotation: entry.open ? 90 : 0

                                Behavior on rotation {
                                    NumberAnimation { duration: 120 }
                                }
                            }

                            Text {
                                text: qsTr("Version %1").arg(entry.modelData.version)
                                color: C.Theme.text
                                font.pixelSize: 15
                                font.weight: Font.ExtraBold
                            }

                            C.Badge {
                                visible: entry.mine
                                small: true
                                tone: "good"
                                text: qsTr("this one")
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Text {
                                Layout.rightMargin: 8
                                visible: !entry.open
                                text: entry.modelData.changes.length === 1 ? qsTr("1 change") : qsTr("%1 changes").arg(entry.modelData.changes.length)
                                color: C.Theme.textDim
                                font.pixelSize: 12
                            }
                        }
                    }

                    ChangeList {
                        visible: entry.open
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        Layout.bottomMargin: 14
                        changes: entry.open ? entry.modelData.changes : []
                    }
                }
            }

            Rectangle {
                visible: earlierButton.visible
                Layout.fillWidth: true
                implicitHeight: 1
                color: Qt.rgba(1, 1, 1, 0.06)
            }

            // The versions before the newest few: listed once asked for
            QQC.AbstractButton {
                id: earlierButton
                property bool firstInFocusChain: false
                property bool lastInFocusChain: false
                readonly property int more: page.up.history.length - page.firstVersions

                visible: more > 0
                Layout.fillWidth: true
                implicitHeight: 44
                hoverEnabled: true
                focusPolicy: Qt.StrongFocus
                Accessible.name: page.allVersions ? qsTr("Earlier versions, hide") : qsTr("Earlier versions, show")
                onClicked: page.allVersions = !page.allVersions
                Keys.onPressed: (event) => C.FocusChain.handle(earlierButton, event)

                background: Rectangle {
                    radius: 10
                    color: Qt.rgba(1, 1, 1, earlierButton.down ? 0.07 : earlierButton.hovered ? 0.04 : 0)

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -2
                        radius: 12
                        color: "transparent"
                        border.width: 2
                        border.color: C.Theme.focus
                        visible: earlierButton.visualFocus
                    }
                }

                contentItem: RowLayout {
                    spacing: 10

                    C.Icon {
                        Layout.leftMargin: 6
                        iconName: "chevron-down"
                        size: 16
                        color: C.Theme.textSoft
                        rotation: page.allVersions ? 180 : 0

                        Behavior on rotation {
                            NumberAnimation { duration: 120 }
                        }
                    }

                    Text {
                        text: page.allVersions ? qsTr("Only the newest versions") : qsTr("Earlier versions")
                        color: C.Theme.textSoft
                        font.pixelSize: 14
                        font.weight: Font.Bold
                    }

                    Item {
                        Layout.fillWidth: true
                    }

                    Text {
                        Layout.rightMargin: 8
                        visible: !page.allVersions
                        text: earlierButton.more === 1 ? qsTr("1 more") : qsTr("%1 more").arg(earlierButton.more)
                        color: C.Theme.textDim
                        font.pixelSize: 12
                    }
                }
            }
        }
    }

    // ------------------------------------------------ updates and logs, side by side where there is room

    GridLayout {
        Layout.fillWidth: true
        columns: page.width >= 740 ? 2 : 1
        columnSpacing: 20
        rowSpacing: 18

        C.SettingsGroup {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 100
            title: qsTr("Updates")

            C.SettingsRow {
                title: qsTr("Check for updates")
                subtitle: qsTr("Asks GitHub at start whether a newer version exists. Nothing is downloaded by itself.")
                keywords: qsTr("update check new version release github")
                textMinimum: 200
                switchItem: updateCheck

                C.Toggle {
                    id: updateCheck
                    Accessible.name: qsTr("Check for updates")
                    checked: Chiaki.settings.updateCheckEnabled
                    onToggled: Chiaki.settings.updateCheckEnabled = checked
                }
            }

            C.SettingsRow {
                title: qsTr("Support the project")
                subtitle: qsTr("A coffee on Ko-fi, opens in your browser.")
                keywords: qsTr("donate ko-fi kofi coffee support developer")
                textMinimum: 200

                KofiButton { }
            }

            C.SettingsRow {
                title: qsTr("Community")
                subtitle: qsTr("Questions, news and people to play with, on Reddit. Opens in your browser.")
                keywords: qsTr("reddit community subreddit forum help questions news friends players")
                textMinimum: 200

                C.PillButton {
                    small: true
                    iconName: "people"
                    text: "Reddit"
                    Accessible.name: qsTr("r/PSRemotePlayTogether on Reddit, opens in your browser")
                    onClicked: Qt.openUrlExternally("https://www.reddit.com/r/PSRemotePlayTogether/")
                }
            }
        }

        C.SettingsGroup {
            Layout.alignment: Qt.AlignTop
            Layout.preferredWidth: 100
            title: qsTr("Logs")

            C.SettingsRow {
                title: qsTr("Hide personal details")
                subtitle: qsTr("Addresses and account IDs are left out, so logs can be shared.")
                keywords: qsTr("logs sanitize privacy hide personal details in logs")
                textMinimum: 200
                switchItem: sanitize

                C.Toggle {
                    id: sanitize
                    Accessible.name: qsTr("Hide personal details in logs")
                    checked: Chiaki.settings.logSanitize
                    onToggled: Chiaki.settings.logSanitize = checked
                }
            }

            C.SettingsRow {
                title: qsTr("Detailed logs")
                subtitle: qsTr("Much more detail, for finding problems.")
                keywords: qsTr("verbose logging debug logs")
                textMinimum: 200
                switchItem: verbose

                C.Toggle {
                    id: verbose
                    Accessible.name: qsTr("Detailed logs")
                    checked: Chiaki.settings.logVerbose
                    onToggled: Chiaki.settings.logVerbose = checked
                }
            }

            C.SettingsRow {
                title: qsTr("Log folder")
                subtitle: Chiaki.settings.logDirectory
                keywords: qsTr("logs files open folder bug report")
                textMinimum: 160

                C.PillButton {
                    small: true
                    iconName: "folder"
                    text: qsTr("Open folder")
                    onClicked: {
                        // "C:/…" on Windows needs the third slash
                        const folder = Chiaki.settings.logDirectory;
                        Qt.openUrlExternally((folder.startsWith("/") ? "file://" : "file:///") + folder);
                    }
                }
            }

            C.SettingsRow {
                title: qsTr("Report a problem")
                subtitle: qsTr("On GitHub, opens in your browser. A log from the folder above helps to find it.")
                keywords: qsTr("bug report issue issues github problem error feedback")
                textMinimum: 200

                C.PillButton {
                    small: true
                    iconName: "external"
                    text: "GitHub"
                    Accessible.name: qsTr("Report a problem on GitHub, opens in your browser")
                    onClicked: Qt.openUrlExternally("https://github.com/Crypto90/PSRemotePlayTogether/issues")
                }
            }
        }
    }

    // ------------------------------------------------ about

    Item {
        // Found by the search and kept on screen as a row
        readonly property bool isSettingRow: true
        readonly property string title: qsTr("About, credits and licenses")
        readonly property string subtitle: qsTr("Who made what this app is built on, and its license.")
        readonly property string keywords: qsTr("about credits license licenses agpl chiaki chiaki-ng street pea florian markl")

        Layout.fillWidth: true
        implicitHeight: aboutLink.implicitHeight

        C.TextLink {
            id: aboutLink
            text: qsTr("About, credits and licenses…")
            onClicked: {
                aboutPopup.byKeys = visualFocus;
                aboutPopup.open();
            }
        }
    }

    // The old About dialog, in the new look: the app, what it is built on, and its license
    QQC.Popup {
        id: aboutPopup
        property bool byKeys: false

        parent: QQC.Overlay.overlay
        x: Math.round((parent.width - width) / 2)
        y: Math.round((parent.height - height) / 2)
        width: Math.min(560, parent.width - 32)
        height: Math.min(aboutContent.implicitHeight + 48 + closeButton.implicitHeight + 20, parent.height - 32)
        padding: 24
        modal: true
        focus: true

        background: Rectangle {
            radius: 18
            color: Qt.rgba(13 / 255, 11 / 255, 31 / 255, 0.97)
            border.width: 1
            border.color: Qt.rgba(140 / 255, 82 / 255, 1, 0.6)
        }
        QQC.Overlay.modal: Rectangle {
            color: Qt.rgba(5 / 255, 5 / 255, 20 / 255, 0.62)
        }

        onAboutToShow: aboutFlick.contentY = 0
        onClosed: {
            if (byKeys) {
                aboutLink.forceActiveFocus(Qt.TabFocusReason);
                // The popup may have given it the focus back already, without the ring
                aboutLink.focusReason = Qt.TabFocusReason;
            }
        }

        contentItem: ColumnLayout {
            spacing: 20

            Flickable {
                id: aboutFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: aboutContent.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                QQC.ScrollBar.vertical: QQC.ScrollBar {
                    policy: aboutFlick.contentHeight > aboutFlick.height ? QQC.ScrollBar.AlwaysOn : QQC.ScrollBar.AlwaysOff
                }

                ColumnLayout {
                    id: aboutContent
                    width: aboutFlick.width - 12
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 16

                        Image {
                            Layout.preferredWidth: 64
                            Layout.preferredHeight: 64
                            source: C.Theme.iconBase + "app-icon.svg"
                            sourceSize: Qt.size(128, 128)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3

                            C.GradientText {
                                text: "PSRemotePlayTogether"
                                font.pixelSize: 21
                                font.weight: Font.ExtraBold
                                from: "#a77bff"
                                to: C.Theme.brandTo
                            }

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Version %1, powered by GamerNova.net").arg(Qt.application.version)
                                wrapMode: Text.WordWrap
                                color: "#e7e4fa"
                                font.pixelSize: 13
                            }
                        }
                    }

                    Repeater {
                        model: [
                            { title: qsTr("Built on chiaki-ng"), text: qsTr("by Street Pea, version %1").arg(Chiaki.chiakiVersion) },
                            { title: qsTr("Fork of Chiaki"), text: qsTr("by Florian Markl, at version 2.1.1") },
                            { title: qsTr("License"), text: qsTr("This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License version 3 as published by the Free Software Foundation.")
                                  + "\n\n" + qsTr("This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.") }
                        ]

                        ColumnLayout {
                            id: credit
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 3

                            C.SectionLabel {
                                text: credit.modelData.title
                            }

                            Text {
                                Layout.fillWidth: true
                                text: credit.modelData.text
                                wrapMode: Text.WordWrap
                                lineHeight: 1.15
                                color: C.Theme.textSoft
                                font.pixelSize: 13
                            }
                        }
                    }
                }
            }

            C.PillButton {
                id: closeButton
                Layout.alignment: Qt.AlignRight
                kind: "primary"
                text: qsTr("Close")
                // Has the focus as soon as the popup opens, with the ring when it was opened with the keys
                focus: true
                onActiveFocusChanged: {
                    if (activeFocus && aboutPopup.byKeys)
                        focusReason = Qt.TabFocusReason;
                }
                firstInFocusChain: true
                lastInFocusChain: true
                onClicked: aboutPopup.close()
                // Up and Down read on, for a controller
                Keys.onUpPressed: aboutFlick.contentY = Math.max(0, aboutFlick.contentY - 60)
                Keys.onDownPressed: aboutFlick.contentY = Math.max(0, Math.min(aboutFlick.contentHeight - aboutFlick.height, aboutFlick.contentY + 60))
            }
        }
    }
}
