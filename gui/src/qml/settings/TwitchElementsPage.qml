import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Twitch elements (Chiaki.twitchElements): what the Twitch channel shows on the game picture
// while you stream, after the design "Switch on, arrange, export for OBS". Each element has
// a switch here and its own place, dragged on the game picture or in the browser; the places
// are the same in OBS and in the app's own stream. At the end, the whole OBS layout as one file.
C.SettingsPage {
    id: page
    title: qsTr("Twitch elements")
    description: qsTr("What shows on the game picture while you stream. Each one is placed on its own – in OBS and in the app’s own stream alike.")

    readonly property var tw: Chiaki.twitchElements
    readonly property var values: tw ? tw.values : ({})
    readonly property var elements: tw ? tw.elements : []
    readonly property var groups: [
        { id: "latest", title: qsTr("Latest") },
        { id: "counters", title: qsTr("Counters") },
        { id: "live", title: qsTr("Live events"), note: qsTr("Only while something runs – gone when it is over.") },
        { id: "all", title: qsTr("All at once") },
    ]
    property string exported: ""

    /** For a link to another page: the settings window does it (SettingsView.openPage). */
    function openPage(key, rowTitle, byKeys) {
        for (let p = page.parent; p; p = p.parent) {
            if (typeof p.openPage === "function") {
                p.openPage(key, rowTitle, byKeys);
                return;
            }
        }
    }

    C.SettingsGroup {
        title: qsTr("Showing them")

        C.SettingsRow {
            visible: !Chiaki.twitch || Chiaki.twitch.state !== "logged_in"
            title: qsTr("Twitch is not connected")
            subtitle: qsTr("The elements fill with your channel once you log in. You can switch them on and arrange them already.")
            textMinimum: 200

            C.PillButton {
                small: true
                text: qsTr("Twitch")
                onClicked: page.openPage("twitch", "", false)
            }
        }

        C.SettingsRow {
            readonly property var lookNames: ({ transparent: qsTr("no boxes"), glass: qsTr("glass boxes"), solid: qsTr("dark boxes") })
            title: qsTr("Look")
            subtitle: qsTr("The same as the other elements, set under Overlays & OBS: %1 on the game picture and in your own stream, %2 in OBS.")
                      .arg(lookNames[Chiaki.hud ? Chiaki.hud.values.style : ""] || lookNames.transparent)
                      .arg(lookNames[Chiaki.overlayLayout ? Chiaki.overlayLayout.values.style : ""] || lookNames.transparent)
            keywords: qsTr("style boxes glass transparent dark twitch elements")
            textMinimum: 200

            C.PillButton {
                small: true
                text: qsTr("Overlays & OBS")
                onClicked: page.openPage("overlays", "", false)
            }
        }

        C.SettingsRow {
            title: qsTr("Arrange on the game picture")
            subtitle: qsTr("While a game is running: open the stream menu and choose Arrange. Drag each element where you want it; the mouse wheel changes its size.")
            keywords: qsTr("arrange move place drag position twitch elements")
            textMinimum: 200

            C.PillButton {
                small: true
                iconName: "refresh"
                text: qsTr("Reset")
                onClicked: page.tw.resetPlaces()
            }
        }

        C.SettingsRow {
            title: qsTr("Arrange in the browser")
            subtitle: Chiaki.overlayUrl.length > 0 ? qsTr("The overlay page with made-up data to drag around. OBS and the app follow at once.")
                                                   : qsTr("Needs the overlay for OBS, under Overlays & OBS.")
            keywords: qsTr("arrange browser edit drag obs twitch elements")
            textMinimum: 200

            C.PillButton {
                small: true
                iconName: "external"
                text: qsTr("Arrange")
                enabled: Chiaki.overlayUrl.length > 0
                onClicked: Qt.openUrlExternally(Chiaki.overlayUrl + "?edit=1")
            }
        }
    }

    Repeater {
        model: page.groups

        C.SettingsGroup {
            id: group
            required property var modelData
            title: modelData.title
            note: modelData.note || ""

            Repeater {
                model: page.elements.filter((e) => e.group === group.modelData.id)

                ColumnLayout {
                    id: elementRows
                    required property var modelData
                    required property int index
                    readonly property string id: modelData.id
                    Layout.fillWidth: true
                    spacing: 0

                    // Counts as a row above this one: the line between two elements stays
                    Item {
                        readonly property bool isSettingRow: true
                        visible: elementRows.index > 0
                        Layout.preferredHeight: 0
                    }

                    C.SettingsRow {
                        readonly property bool joinedAway: elementRows.id === "uptime" && !!page.values.uptime_joined && !!page.values.viewers_show
                        image: "qrc:/twitch/" + elementRows.modelData.icon + ".svg"
                        title: elementRows.modelData.name
                        subtitle: joinedAway && elementToggle.checked ? elementRows.modelData.what + " – " + qsTr("next to the viewers") : elementRows.modelData.what
                        keywords: qsTr("twitch element overlay") + " " + elementRows.id
                        textMinimum: 200
                        switchItem: elementToggle

                        C.Toggle {
                            id: elementToggle
                            Accessible.name: elementRows.modelData.name
                            checked: !!page.values[elementRows.id + "_show"]
                            onToggled: page.tw.set(elementRows.id + "_show", checked)
                        }
                    }

                    // The top cheerer: of this stream or of all time
                    C.SettingsRow {
                        visible: elementRows.id === "topcheer"
                        sub: true
                        enabled: elementToggle.checked
                        title: qsTr("Top cheerer of")
                        textMinimum: 120

                        C.Segmented {
                            model: [qsTr("This stream"), qsTr("All time")]
                            currentIndex: page.values.topcheer_span === "all" ? 1 : 0
                            onActivated: (index) => page.tw.set("topcheer_span", index === 1 ? "all" : "stream")
                        }
                    }

                    // Viewers and uptime: one pill or two
                    C.SettingsRow {
                        visible: elementRows.id === "uptime"
                        sub: true
                        enabled: elementToggle.checked && !!page.values.viewers_show
                        title: qsTr("In one pill with the viewers")
                        textMinimum: 120
                        switchItem: joinToggle

                        C.Toggle {
                            id: joinToggle
                            Accessible.name: qsTr("Uptime in one pill with the viewers")
                            checked: !!page.values.uptime_joined
                            onToggled: page.tw.set("uptime_joined", checked)
                        }
                    }
                }
            }
        }
    }

    C.SettingsGroup {
        title: qsTr("OBS layout")

        C.SettingsRow {
            image: "qrc:/icons/ui/download.svg"
            title: qsTr("Export for OBS")
            subtitle: page.exported.length > 0 ? qsTr("Saved as %1. In OBS: Scene Collection → Import.").arg(page.exported)
                                               : qsTr("One file with every scene, ready to import. In OBS: Scene Collection → Import.")
            keywords: qsTr("obs export scene collection import layout file")
            textMinimum: 200

            C.PillButton {
                small: true
                kind: "primary"
                iconName: "download"
                text: qsTr("Export for OBS")
                onClicked: {
                    const file = Chiaki.scenes ? Chiaki.scenes.exportObs() : "";
                    if (file.length > 0)
                        page.exported = file;
                }
            }
        }

        C.SettingsRow {
            sub: true
            title: qsTr("In the file")
            textMinimum: 120

            Flow {
                Layout.preferredWidth: 420
                spacing: 8
                Repeater {
                    model: {
                        const list = [qsTr("Game with overlay")];
                        for (const s of (Chiaki.scenes ? Chiaki.scenes.scenes : []))
                            if (s.id !== "game")
                                list.push(s.name);
                        list.push(qsTr("Alerts on every scene"));
                        return list;
                    }
                    C.Badge {
                        required property string modelData
                        text: modelData
                    }
                }
            }
        }
    }
}
