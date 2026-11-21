import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC

import org.streetpea.chiaking

import "../controls" as C

// Twitch: the player's account and what their channel tells the app (Chiaki.twitch).
// Logging in shows a code that the player confirms on twitch.tv/activate: the app never
// sees the password. Alerts and viewer actions are built on it.
C.SettingsPage {
    id: page
    title: qsTr("Twitch")
    description: qsTr("Log in with your Twitch channel to show alerts for follows, subs, bits, raids, channel points and more, in your stream and in OBS. The app talks to Twitch directly.")

    readonly property var twitch: Chiaki.twitch
    readonly property string state: twitch ? twitch.state : "no_app"

    /** What each kind of event is called here. */
    readonly property var eventNames: ({
        "channel.follow": qsTr("Follows"),
        "channel.subscribe": qsTr("Subscriptions"),
        "channel.subscription.message": qsTr("Resubs"),
        "channel.subscription.gift": qsTr("Gift subs"),
        "channel.cheer": qsTr("Bits"),
        "channel.raid": qsTr("Raids"),
        "channel.channel_points_custom_reward_redemption.add": qsTr("Channel points: your rewards"),
        "channel.channel_points_automatic_reward_redemption.add": qsTr("Channel points: Twitch's own"),
        "channel.hype_train.begin": qsTr("Hype train starts"),
        "channel.hype_train.progress": qsTr("Hype train levels"),
        "channel.hype_train.end": qsTr("Hype train ends"),
        "channel.poll.begin": qsTr("Polls start"),
        "channel.poll.progress": qsTr("Poll votes"),
        "channel.poll.end": qsTr("Polls end"),
        "channel.prediction.begin": qsTr("Predictions start"),
        "channel.prediction.progress": qsTr("Prediction points"),
        "channel.prediction.lock": qsTr("Predictions close"),
        "channel.prediction.end": qsTr("Predictions end"),
        "channel.goal.begin": qsTr("Goals start"),
        "channel.goal.progress": qsTr("Goal progress"),
        "channel.goal.end": qsTr("Goals end"),
        "channel.charity_campaign.donate": qsTr("Charity donations"),
        "channel.charity_campaign.start": qsTr("Charity campaigns start"),
        "channel.charity_campaign.progress": qsTr("Charity campaign totals"),
        "channel.charity_campaign.stop": qsTr("Charity campaigns end"),
        "channel.chat.message": qsTr("Chat messages"),
        "channel.chat.message_delete": qsTr("Deleted chat messages"),
        "channel.chat.clear_user_messages": qsTr("Chat of banned viewers"),
        "channel.chat.clear": qsTr("Cleared chat"),
        "channel.shoutout.receive": qsTr("Shoutouts"),
        "channel.ad_break.begin": qsTr("Ad breaks"),
        "stream.online": qsTr("Going live"),
        "stream.offline": qsTr("Going offline")
    })

    C.SettingsGroup {
        title: qsTr("Your account")

        C.SettingsRow {
            visible: page.state === "no_app"
            title: qsTr("Not in this build")
            subtitle: qsTr("This copy of the app has no id at Twitch, so it cannot log in. The project's releases have one; builds of your own set PSRPT_TWITCH_CLIENT_ID.")
            textMinimum: 200
        }

        C.SettingsRow {
            visible: page.state === "logged_out"
            title: qsTr("Not logged in")
            subtitle: page.twitch && page.twitch.error ? page.twitch.error
                     : qsTr("You confirm a code on twitch.tv: the app never sees your password. The login is kept in %1.")
                       .arg(Chiaki.live && Chiaki.live.keyStore ? Chiaki.live.keyStore : qsTr("this computer's memory until the app quits"))
            keywords: qsTr("twitch login sign in account channel")
            textMinimum: 200

            C.PillButton {
                kind: "brand"
                text: qsTr("Log in with Twitch")
                onClicked: page.twitch.logIn()
            }
        }

        C.SettingsRow {
            visible: page.state === "code"
            title: qsTr("Confirm this code on Twitch")
            subtitle: qsTr("Open twitch.tv/activate, check that it shows this code, and allow the app. This page goes on by itself.")
            textMinimum: 200

            ColumnLayout {
                spacing: 10

                Text {
                    Layout.alignment: Qt.AlignRight
                    text: page.twitch ? page.twitch.userCode : ""
                    color: C.Theme.text
                    font.pixelSize: 30
                    font.weight: Font.ExtraBold
                    font.letterSpacing: 4
                    // A code to type, not a secret: it only works with the player's own login
                    textFormat: Text.PlainText
                }

                RowLayout {
                    spacing: 8

                    C.PillButton {
                        small: true
                        kind: "primary"
                        iconName: "external"
                        text: qsTr("Open Twitch")
                        onClicked: page.twitch.openVerify()
                    }

                    C.PillButton {
                        small: true
                        iconName: "copy"
                        text: qsTr("Copy code")
                        onClicked: page.twitch.copyCode()
                    }

                    C.PillButton {
                        small: true
                        text: qsTr("Cancel")
                        onClicked: page.twitch.cancelLogIn()
                    }
                }
            }
        }

        C.SettingsRow {
            visible: page.state === "logged_in"
            title: qsTr("Logged in as %1").arg(page.twitch ? page.twitch.login : "")
            subtitle: qsTr("The app may read what happens on your channel and its chat, and make and complete its own channel point rewards. It never writes in the chat.")
            keywords: qsTr("twitch account logout")
            textMinimum: 200

            C.PillButton {
                small: true
                kind: "danger"
                text: qsTr("Log out")
                onClicked: page.twitch.logOut()
            }
        }
    }

    C.SettingsGroup {
        visible: page.state === "logged_in"
        title: qsTr("Events from your channel")
        note: qsTr("Some events need an Affiliate or Partner channel (channel points, bits, subs, hype trains, polls, predictions, goals).")

        C.SettingsRow {
            title: {
                const s = page.twitch ? page.twitch.events : "off";
                return s === "on" ? qsTr("Receiving events") : s === "connecting" ? qsTr("Connecting to Twitch…")
                     : s === "error" ? qsTr("Connection lost, trying again") : qsTr("Not connected");
            }
            subtitle: page.twitch && page.twitch.channelLive ? qsTr("Your channel is live on Twitch right now.") : ""
            textMinimum: 200

            C.Badge {
                dot: true
                tone: page.twitch && page.twitch.events === "on" ? "good" : "wait"
                text: page.twitch && page.twitch.events === "on" ? qsTr("Connected") : qsTr("Waiting")
            }
        }

        C.SettingsRow {
            title: qsTr("Show my stream in the room list")
            subtitle: qsTr("While you are live, your public room says so, and people browsing the rooms can watch you on Twitch.")
            keywords: qsTr("twitch public room list live badge")
            textMinimum: 200

            C.Toggle {
                Accessible.name: qsTr("Show my stream in the room list")
                checked: !!(page.twitch && page.twitch.showInRooms)
                onToggled: page.twitch.showInRooms = checked
            }
        }

        Repeater {
            model: page.twitch ? page.twitch.subscriptions : []

            C.SettingsRow {
                required property var modelData
                sub: true
                title: page.eventNames[modelData.type] || modelData.type
                subtitle: modelData.ok ? "" : modelData.problem
                textMinimum: 200

                C.Badge {
                    tone: modelData.ok ? "good" : "neutral"
                    text: modelData.ok ? qsTr("On") : qsTr("Not available")
                }
            }
        }
    }
}
