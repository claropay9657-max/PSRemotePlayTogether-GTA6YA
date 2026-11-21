import QtQuick
import QtQuick.Layouts

import "../controls" as C

// Viewer actions: a channel point reward of the app's own, bits, a sub, a follow or a raid
// presses a button in the game (Chiaki.viewerActions). Off until the player switches it on.
C.SettingsPage {
    id: page
    title: qsTr("Viewer actions")
    description: qsTr("Your viewers press a button in the game: with channel points, bits, a sub, a follow or a raid. It is on your own controller, for a moment, one after the other. Off until you switch it on; Stop all is also in the stream menu, under Alerts.")

    readonly property var va: Chiaki.viewerActions
    readonly property bool loggedIn: !!Chiaki.twitch && Chiaki.twitch.state === "logged_in"
    readonly property var triggerKeys: ["points", "bits", "sub", "follow", "raid"]
    readonly property var triggerNames: [qsTr("Channel points"), qsTr("Bits"), qsTr("A sub"), qsTr("A follow"), qsTr("A raid")]
    readonly property var inputKeys: ["cross", "circle", "square", "triangle", "l1", "r1", "l2", "r2", "l3", "r3", "options",
        "up", "down", "left", "right", "ls_up", "ls_down", "ls_left", "ls_right", "rs_up", "rs_down", "rs_left", "rs_right"]
    readonly property var inputNames: ["✕", "○", "□", "△", "L1", "R1", "L2", "R2", "L3", "R3", "Options",
        qsTr("D-pad up"), qsTr("D-pad down"), qsTr("D-pad left"), qsTr("D-pad right"),
        qsTr("Left stick up"), qsTr("Left stick down"), qsTr("Left stick left"), qsTr("Left stick right"),
        qsTr("Right stick up"), qsTr("Right stick down"), qsTr("Right stick left"), qsTr("Right stick right")]
    readonly property var holdValues: [100, 200, 500, 1000, 2000]
    readonly property var holdNames: [qsTr("A tap"), qsTr("A short press"), qsTr("Half a second"), qsTr("One second"), qsTr("Two seconds")]

    function openPage(key, rowTitle, byKeys) {
        for (let p = page.parent; p; p = p.parent) {
            if (typeof p.openPage === "function") {
                p.openPage(key, rowTitle, byKeys);
                return;
            }
        }
    }

    C.SettingsGroup {
        title: qsTr("Letting viewers play along")

        C.SettingsRow {
            visible: !page.loggedIn
            title: qsTr("Twitch is not connected")
            subtitle: qsTr("Channel points, bits, subs, follows and raids come from your channel once you log in.")
            textMinimum: 200

            C.PillButton {
                small: true
                text: qsTr("Twitch")
                onClicked: page.openPage("twitch", "", false)
            }
        }

        C.SettingsRow {
            title: qsTr("Let viewers press buttons")
            subtitle: qsTr("While this is off, the rewards cannot be redeemed and nothing is pressed.")
            keywords: qsTr("viewer actions channel points control twitch")
            textMinimum: 200
            switchItem: enabledToggle

            C.Toggle {
                id: enabledToggle
                Accessible.name: qsTr("Let viewers press buttons")
                checked: !!page.va && page.va.enabled
                onToggled: page.va.enabled = checked
            }
        }

        C.SettingsRow {
            title: qsTr("At most per minute")
            subtitle: qsTr("More than that in one minute are not run; channel points go back to the viewer.")
            textMinimum: 200

            C.Stepper {
                from: 1
                to: 60
                value: page.va ? page.va.perMinute : 6
                onMoved: (v) => page.va.perMinute = v
            }
        }

        C.SettingsRow {
            title: qsTr("Stop all")
            subtitle: page.va && page.va.last ? qsTr("Last: %1").arg(page.va.last) : qsTr("Lets every button go, drops what is waiting and switches viewer actions off.")
            textMinimum: 200

            C.PillButton {
                small: true
                kind: "danger"
                text: qsTr("Stop all")
                onClicked: page.va.stopAll()
            }
        }
    }

    C.SettingsGroup {
        title: qsTr("Actions")
        note: page.va && page.va.status ? page.va.status
            : qsTr("A channel point action becomes a reward on your channel, made and kept up to date by the app. Channel points need an Affiliate or Partner channel.")

        Repeater {
            model: page.va ? page.va.actions : []

            C.SettingsRow {
                id: row
                required property var modelData
                required property int index
                title: modelData.title
                subtitle: modelData.trigger === "points" ? qsTr("%1 channel points · %2").arg(modelData.cost).arg(modelData.reward ? qsTr("on Twitch") : qsTr("not on Twitch yet"))
                    : modelData.trigger === "bits" ? qsTr("From %1 bits").arg(modelData.min)
                    : page.triggerNames[page.triggerKeys.indexOf(modelData.trigger)]
                textMinimum: 160

                ColumnLayout {
                    spacing: 6

                    RowLayout {
                        spacing: 6

                        C.TextField {
                            Layout.preferredWidth: 170
                            text: row.modelData.title
                            maximumLength: 45
                            Accessible.name: qsTr("Name of the action")
                            onEditingFinished: if (text !== row.modelData.title) page.va.setAction(row.index, { title: text })
                        }

                        C.ComboBox {
                            Layout.preferredWidth: 150
                            model: page.triggerNames
                            currentIndex: Math.max(0, page.triggerKeys.indexOf(row.modelData.trigger))
                            Accessible.name: qsTr("What triggers it")
                            onActivated: (i) => page.va.setAction(row.index, { trigger: page.triggerKeys[i] })
                        }

                        C.TextField {
                            Layout.preferredWidth: 80
                            visible: row.modelData.trigger === "points" || row.modelData.trigger === "bits"
                            text: String(row.modelData.trigger === "bits" ? row.modelData.min : row.modelData.cost)
                            inputMethodHints: Qt.ImhDigitsOnly
                            validator: IntValidator { bottom: 1; top: 1000000 }
                            Accessible.name: row.modelData.trigger === "bits" ? qsTr("Fewest bits") : qsTr("Channel points it costs")
                            onEditingFinished: page.va.setAction(row.index, row.modelData.trigger === "bits" ? { min: parseInt(text) } : { cost: parseInt(text) })
                        }
                    }

                    RowLayout {
                        spacing: 6

                        C.ComboBox {
                            Layout.preferredWidth: 150
                            model: page.inputNames
                            currentIndex: Math.max(0, page.inputKeys.indexOf(row.modelData.input))
                            Accessible.name: qsTr("Button")
                            onActivated: (i) => page.va.setAction(row.index, { input: page.inputKeys[i] })
                        }

                        C.ComboBox {
                            Layout.preferredWidth: 150
                            model: page.holdNames
                            currentIndex: Math.max(0, page.holdValues.indexOf(row.modelData.hold))
                            Accessible.name: qsTr("How long")
                            onActivated: (i) => page.va.setAction(row.index, { hold: page.holdValues[i] })
                        }

                        C.Toggle {
                            checked: row.modelData.on
                            Accessible.name: qsTr("Action on")
                            onToggled: page.va.setAction(row.index, { on: checked })
                        }

                        C.PillButton {
                            small: true
                            text: qsTr("Try")
                            Accessible.description: row.modelData.title
                            onClicked: page.va.tryAction(row.index)
                        }

                        C.PillButton {
                            small: true
                            kind: "danger"
                            text: qsTr("Remove")
                            Accessible.description: row.modelData.title
                            onClicked: page.va.removeAction(row.index)
                        }
                    }
                }
            }
        }

        C.SettingsRow {
            title: page.va && page.va.actions.length ? qsTr("Another action") : qsTr("Your first action")
            subtitle: qsTr("For example ✕ for 500 channel points, or a jump for every raid.")
            textMinimum: 200

            C.PillButton {
                small: true
                kind: "primary"
                text: qsTr("Add")
                enabled: !!page.va && page.va.actions.length < 20
                onClicked: page.va.addAction()
            }
        }
    }
}
