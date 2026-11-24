import QtQuick
import QtQuick.Layouts
import QtQuick.Controls

import "controls" as C

// A plan in a chat: "When shall we play?" with a few times. Everybody answers each time with
// yes, maybe or no; each time shows how many said yes and maybe, the best one so far stands
// out, and this player's own answers can be changed at any time.
//
// `plan` is {title, times (ms)}, `votes` {fp: [answers]} (0 no, 1 maybe, 2 yes).
Item {
    id: card

    property var plan: ({})
    property var votes: ({})
    /** This player's fingerprint, to find and show the own answers. */
    property string me: ""
    property bool mine: false
    /** Who suggested it (or "You"). */
    property string person: ""
    property string time: ""
    /** How many can answer: the friend and this player, or the group's members. */
    property int people: 2
    signal answer(var answers)

    readonly property var times: plan.times || []
    readonly property var own: votes[me] || []
    /** Per time: [yes, maybe]. */
    readonly property var tally: {
        const out = [];
        for (let i = 0; i < times.length; ++i)
            out.push([0, 0]);
        for (const fp in votes) {
            const a = votes[fp] || [];
            for (let i = 0; i < times.length; ++i) {
                if (a[i] === 2)
                    out[i][0]++;
                else if (a[i] === 1)
                    out[i][1]++;
            }
        }
        return out;
    }
    /** The time most can make: most yes, then most maybe; -1 while nobody answered yes or maybe. */
    readonly property int best: {
        let best = -1;
        for (let i = 0; i < tally.length; ++i) {
            if (tally[i][0] + tally[i][1] === 0)
                continue;
            if (best < 0 || tally[i][0] > tally[best][0] || (tally[i][0] === tally[best][0] && tally[i][1] > tally[best][1]))
                best = i;
        }
        return best;
    }

    function dayText(ms) {
        const d = new Date(ms);
        const today = new Date();
        const tomorrow = new Date(today.getFullYear(), today.getMonth(), today.getDate() + 1);
        if (d.toDateString() === today.toDateString())
            return qsTr("Today");
        if (d.toDateString() === tomorrow.toDateString())
            return qsTr("Tomorrow");
        return Qt.formatDate(d, "ddd d MMM");
    }

    function setAnswer(index, value) {
        const a = [];
        for (let i = 0; i < times.length; ++i)
            a.push(i === index ? value : (own[i] || 0));
        card.answer(a);
    }

    implicitWidth: 340
    implicitHeight: shape.implicitHeight

    Rectangle {
        id: shape
        width: card.width
        implicitHeight: column.implicitHeight + 24
        radius: 18
        color: "#161231"
        border.width: 1
        border.color: Qt.rgba(0, 242 / 255, 1, 0.30)
        clip: true

        // A soft band of the plan's colours behind the top: rounded on top, straight below
        Item {
            width: parent.width
            height: 64
            clip: true

            Rectangle {
                width: parent.width
                height: parent.height + 20
                radius: 18
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0.0; color: Qt.rgba(0, 0.55, 0.75, 0.45) }
                    GradientStop { position: 1.0; color: Qt.rgba(0.55, 0.32, 1.0, 0.35) }
                }
            }

            // Fading into the card below
            Rectangle {
                anchors.fill: parent
                gradient: Gradient {
                    GradientStop { position: 0.5; color: "transparent" }
                    GradientStop { position: 1.0; color: "#161231" }
                }
            }

            C.Icon {
                x: parent.width - 84
                y: -10
                iconName: "calendar"
                size: 96
                color: Qt.rgba(1, 1, 1, 0.08)
            }
        }

        ColumnLayout {
            id: column
            x: 14
            y: 12
            width: parent.width - 28
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    implicitWidth: planWord.implicitWidth + 16
                    implicitHeight: 20
                    radius: 10
                    gradient: Gradient {
                        orientation: Gradient.Horizontal
                        GradientStop { position: 0.0; color: "#00b8d4" }
                        GradientStop { position: 1.0; color: C.Theme.brandFrom }
                    }

                    Text {
                        textFormat: Text.PlainText
                        id: planWord
                        anchors.centerIn: parent
                        text: qsTr("PLAN")
                        color: "#ffffff"
                        font.pixelSize: 10
                        font.weight: Font.ExtraBold
                        font.letterSpacing: 1.2
                    }
                }

                Text {
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                    color: Qt.rgba(1, 1, 1, 0.92)
                    font.pixelSize: 12
                    font.weight: Font.DemiBold
                    text: card.mine ? qsTr("You asked") : qsTr("%1 asks").arg(card.person)
                }

                Text {
                    textFormat: Text.PlainText
                    color: C.Theme.textDim
                    font.pixelSize: 10
                    text: card.time
                }
            }

            Text {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                color: C.Theme.text
                font.pixelSize: 17
                font.weight: Font.ExtraBold
                text: card.plan.title ? qsTr("When: %1?").arg(card.plan.title) : qsTr("When shall we play?")
            }

            Repeater {
                model: card.times.length

                delegate: Rectangle {
                    id: slot
                    required property int index
                    readonly property bool best: card.best === index
                    readonly property int yes: card.tally[index] ? card.tally[index][0] : 0
                    readonly property int maybe: card.tally[index] ? card.tally[index][1] : 0
                    readonly property int mineAnswer: card.own[index] || 0
                    Layout.fillWidth: true
                    implicitHeight: slotColumn.implicitHeight + 18
                    radius: 14
                    color: best ? Qt.rgba(0.20, 0.83, 0.60, 0.10) : Qt.rgba(1, 1, 1, 0.04)
                    border.width: 1
                    border.color: best ? Qt.rgba(0.20, 0.83, 0.60, 0.45) : Qt.rgba(1, 1, 1, 0.08)

                    ColumnLayout {
                        id: slotColumn
                        x: 12
                        y: 9
                        width: parent.width - 24
                        spacing: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            ColumnLayout {
                                spacing: 0

                                Text {
                                    textFormat: Text.PlainText
                                    color: C.Theme.textSoft
                                    font.pixelSize: 11
                                    font.weight: Font.DemiBold
                                    text: card.dayText(card.times[slot.index])
                                }

                                Text {
                                    textFormat: Text.PlainText
                                    color: C.Theme.text
                                    font.pixelSize: 20
                                    font.weight: Font.ExtraBold
                                    text: Qt.formatTime(new Date(card.times[slot.index]), "hh:mm")
                                }
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            C.Badge {
                                visible: slot.best
                                small: true
                                tone: "good"
                                dot: true
                                text: qsTr("Best so far")
                            }
                        }

                        // How many can: yes filled, maybe lighter, of everybody
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 6
                                radius: 3
                                color: Qt.rgba(1, 1, 1, 0.08)

                                Rectangle {
                                    width: parent.width * Math.min(1, (slot.yes + slot.maybe) / Math.max(1, card.people))
                                    height: parent.height
                                    radius: 3
                                    color: Qt.rgba(0.96, 0.73, 0.26, 0.55)
                                }

                                Rectangle {
                                    width: parent.width * Math.min(1, slot.yes / Math.max(1, card.people))
                                    height: parent.height
                                    radius: 3
                                    color: C.Theme.good
                                }
                            }

                            Text {
                                textFormat: Text.PlainText
                                color: C.Theme.textSoft
                                font.pixelSize: 11
                                text: slot.maybe > 0 ? qsTr("%1 yes · %2 maybe").arg(slot.yes).arg(slot.maybe) : qsTr("%1 yes").arg(slot.yes)
                            }
                        }

                        // The own answer
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6

                            Repeater {
                                model: [
                                    { value: 2, label: qsTr("Yes"), color: C.Theme.good },
                                    { value: 1, label: qsTr("Maybe"), color: C.Theme.wait },
                                    { value: 0, label: qsTr("No"), color: C.Theme.bad }
                                ]

                                delegate: AbstractButton {
                                    id: choice
                                    required property var modelData
                                    readonly property bool chosen: card.own.length > slot.index && slot.mineAnswer === modelData.value
                                    Layout.fillWidth: true
                                    implicitHeight: 30
                                    hoverEnabled: true
                                    Accessible.name: modelData.label
                                    onClicked: card.setAnswer(slot.index, modelData.value)

                                    background: Rectangle {
                                        radius: 15
                                        color: choice.chosen ? Qt.rgba(choice.modelData.color.r, choice.modelData.color.g, choice.modelData.color.b, 0.22)
                                                             : Qt.rgba(1, 1, 1, choice.hovered ? 0.08 : 0.04)
                                        border.width: choice.chosen || choice.visualFocus ? 1 : 0
                                        border.color: choice.visualFocus ? C.Theme.focus : choice.modelData.color
                                    }

                                    contentItem: Text {
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        color: choice.chosen ? "#ffffff" : C.Theme.textSoft
                                        font.pixelSize: 12
                                        font.weight: choice.chosen ? Font.ExtraBold : Font.DemiBold
                                        text: choice.modelData.label
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
