import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Updates & logs: which version this is and whether a newer one is out, the update
// check, logs for bug reports, and the credits and licenses.
C.SettingsPage {
    id: page
    title: qsTr("Updates & logs")
    description: qsTr("Which version you have, newer ones, and logs for bug reports.")

    /** A newer release than this build, found by the update check; "" when none is known. */
    readonly property string newer: Chiaki.updateVersion || ""
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

                C.PillButton {
                    visible: page.newer.length > 0
                    Layout.alignment: about.narrow ? Qt.AlignLeft : Qt.AlignRight
                    implicitHeight: 40
                    kind: "primary"
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
