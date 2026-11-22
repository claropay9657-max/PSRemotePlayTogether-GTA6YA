import QtQuick
import QtQuick.Layouts

import org.streetpea.chiaking

import "../controls" as C

// Permissions (on a Mac only): what macOS has to allow the app, each with one click: macOS asks
// in its own window where it still asks, otherwise the click opens the right place in System
// Settings (Chiaki.permissions, macpermissions.h).
C.SettingsPage {
    id: page
    title: qsTr("Permissions")
    description: qsTr("What macOS has to allow the app, and what each is for. Allow only what you use.")

    readonly property var perms: Chiaki.permissions
    /** What each is for and where it is in System Settings, by its key. */
    readonly property var info: ({
        localNetwork: { title: qsTr("Devices on your network"), what: qsTr("Finding, waking and playing your consoles at home."), where: qsTr("Local Network") },
        microphone: { title: qsTr("Microphone"), what: qsTr("Talking in the voice chat."), where: qsTr("Microphone") },
        camera: { title: qsTr("Camera"), what: qsTr("Your webcam for the others."), where: qsTr("Camera") },
        screen: { title: qsTr("Screen & system audio recording"), what: qsTr("Sharing a game on this Mac, with its sound."), where: qsTr("Screen & System Audio Recording") },
        accessibility: { title: qsTr("Accessibility"), what: qsTr("Friends' mouse and keyboard in a game on this Mac."), where: qsTr("Accessibility") }
    })
    readonly property bool anyToAsk: (perms.items || []).some((item) => item.state === "ask")

    // Coming back from System Settings refreshes by itself; this catches a change made while
    // the app stays in front
    Timer {
        interval: 2000
        repeat: true
        running: page.enabled && page.visible
        onTriggered: page.perms.refresh()
    }
    onEnabledChanged: if (enabled) perms.refresh()

    C.SettingsGroup {
        title: qsTr("Allowed by macOS")

        C.SettingsRow {
            visible: page.anyToAsk
            title: qsTr("Allow all at once")
            subtitle: qsTr("macOS asks for each in its own window, one after the other. For screen recording and accessibility it shows the place in System Settings to switch them on.")
            keywords: qsTr("permissions allow all privacy grant")

            C.PillButton {
                kind: "primary"
                small: true
                text: qsTr("Allow all")
                onClicked: page.perms.allowAll()
            }
        }

        Repeater {
            model: page.perms.items

            C.SettingsRow {
                id: row
                required property var modelData
                readonly property var about: page.info[modelData.key] || ({ title: modelData.key, what: "", where: "" })
                readonly property string state: modelData.state
                title: about.title
                subtitle: about.what + " " + (state === "allowed" ? qsTr("Allowed.")
                          : state === "ask" ? qsTr("Not asked yet: one click and macOS asks.")
                          : qsTr("Off. Switch it on under System Settings → Privacy & Security → %1.").arg(about.where))
                keywords: qsTr("permission privacy allow %1").arg(modelData.key)

                C.Badge {
                    small: true
                    dot: true
                    tone: row.state === "allowed" ? "good" : row.state === "ask" ? "brand" : "wait"
                    text: row.state === "allowed" ? qsTr("Allowed") : row.state === "ask" ? qsTr("Not asked") : qsTr("Off")
                }

                C.PillButton {
                    visible: row.state !== "allowed"
                    small: true
                    kind: row.state === "ask" ? "primary" : "ghost"
                    iconName: row.state === "ask" ? "" : "external"
                    iconAfter: true
                    text: row.state === "ask" ? qsTr("Allow") : qsTr("Open System Settings")
                    Accessible.name: row.state === "ask" ? qsTr("Allow %1").arg(row.about.title)
                                                          : qsTr("Open %1 in System Settings").arg(row.about.where)
                    onClicked: page.perms.allow(row.modelData.key)
                }
            }
        }

        // Screen recording just switched on: macOS lets the app record after it starts again
        C.SettingsRow {
            visible: page.perms.restartNeeded
            title: qsTr("Quit and open the app again")
            subtitle: qsTr("macOS lets the app record the screen only after it has started again.")
            keywords: qsTr("restart screen recording")
        }
    }

    C.SettingsGroup {
        title: qsTr("Good to know")

        C.SettingsRow {
            title: qsTr("After an update")
            subtitle: qsTr("macOS may take a new version of the app for another app and ask again, or list it twice in System Settings. Old entries do no harm.")
            keywords: qsTr("update permissions twice duplicate entries list")
        }
    }
}
