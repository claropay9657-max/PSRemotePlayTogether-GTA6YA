# Friends, chat and status (design)

Status: built (November 2026), not yet released or reviewed from outside. The exact protocol is
docs/friends-protocol.md; the code: coplay/src/friends_crypto.cpp (the cryptography),
gui/src/friends.cpp (the app's client), web/js/friends/ (the browser's), server/friends/ (the
server). The owner's decisions are at the end.

What it is for: players who know each other should be able to talk, agree on a time and find each
other's rooms also when no session runs. That means a friends list with who is online and what
they do, private chats and group chats, a way to agree on a time, notifications while the app sits
in the tray, and moderation: blocking people, and removing and banning them from groups.

## Ground rules

- **The project stands on its own.** No account of any other service is needed or used. What has
  to run on a server is one self-contained PHP file, `server/friends/psrpt_friends.php`, in the
  style of the meeting point (`server/meeting-point/psrpt_meet.php`) and the room list
  (`server/rooms/psrpt_rooms.php`): PHP 7.4 or newer, no database, no dependencies, its data in a
  folder of files. Anybody can run it. The app and the page use the one on gamernova.net unless
  the player names another under **Settings → Network**, or switches it off.
- **No accounts, no passwords, no e-mail.** A player is a key pair that their app or browser makes
  by itself. Friends find each other by a link, as they find a session by an invite.
- **The server cannot read anything, now or later.** Names, pictures, statuses and messages are
  sealed between friends (end to end), and the keys of messages move on with every message (forward
  secrecy): whoever steals a device's keys cannot read the messages before, and after a few
  messages back and forth not the ones after either. The server keeps sealed notes in boxes with
  random names, for a limited time, as the meeting point does. What it does see: box names, sizes,
  times, and the addresses of whoever talks to it, like any web server.
- **A session's own traffic never goes through it.** Picture, sound and controllers still go
  directly between the players. The friends server only carries small notes: requests, statuses,
  messages, invites, and the public keys that let friends start talking.
- **Everything stays optional.** Joining by an invite or a link, and the browser page, keep
  working without ever making a friend.
- **The app and the page are equal.** A player who only uses a web browser has friends, chats,
  status and notifications like a player with the app ("Web only" below). The page does what the
  app does wherever a browser allows it, with the same words and in the same places.
- **No cryptography of our own invention.** Private chats follow Signal's published X3DH and Double
  Ratchet, groups its Sender Keys, with one change: the curve P-256 instead of Curve25519, because
  every current browser has P-256 in WebCrypto and OpenSSL has it in the app. Both clients are held
  to the same test vectors, and the protocol is reviewed by somebody who knows cryptography before
  real messages go through it.

## The pieces

### 1. Who a player is, and their devices

- **The identity**, made on the first device: an identity key pair to sign (ECDSA, P-256) and one
  to agree on keys (ECDH, P-256), the second signed by the first. The player's **fingerprint** is
  the first 16 bytes of the SHA-256 of the two public keys; friends know a player by it.
- **Every device of a player is a device of its own**: the app on two computers, a browser, and so
  on. Each has its own keys for starting conversations:
  - a **signed prekey** (ECDH), signed with the identity key, renewed every week;
  - a pool of **one-time prekeys** (about 50), each used for one first message and then deleted.
  The public halves are on the server, so that a friend can start a conversation with a device that
  is offline. The private halves never leave the device.
- **The card**: the identity's public keys and the list of the player's devices, signed with the
  identity key. The server keeps it under the fingerprint. Friends fetch it again when its version
  changes, and so learn of a new or removed device.
- **The friend link.** `https://<page>/#f=<fingerprint>.<link key>` (the browser page) and
  `psrpt://friend/<fingerprint>.<link key>` (the app), plus `?at=<server>` for a friends server
  other than the default; about 50 characters. The **link key** opens the name and picture the
  player put on their card for whoever has the link: the server keeps them sealed and cannot read
  them. Copied, shown as a QR code, or sent along with an invite.
- **Name and picture** are the ones of the player's profile (as in sessions) and go only to
  friends, sealed.
- **Kept safe:** in the app, the private keys go into the system's password store (`keystore.h`:
  the macOS keychain, the Windows Credential Manager, the Secret Service on Linux); in a browser,
  into IndexedDB as keys that cannot be read out of WebCrypto. The ratchets' state (below) lives with
  the chats, sealed with a key from that store.
- **Linking another device:** **Link a device** on a device that has the identity shows a QR code
  and a short code; the new device scans or types it. The two meet in a **link box** on the server
  for two minutes, agree on a key there (ECDH, one side committing to its key first; both sides confirm the same six digits on screen), and
  the old device hands over, sealed: the identity's private keys, the friends, the groups and the
  settings, and if wanted the chat history. The new device makes its own prekeys, the card gets the
  new device, and friends' devices start conversations with it by themselves. The private keys of
  the old device's conversations are never copied: a device's ratchets are its own.
- **Removing a device** (lost, sold) from any other device of the player: it leaves the card, and
  friends stop sealing anything for it. **New identity** is for when the identity's own keys may
  have been stolen: friends then see "Mara's keys changed" and have to confirm, as in Signal.
- **Lost all devices, lost friends.** Without accounts there is nothing to recover from. So the app
  and the page offer a **backup**: a file with the identity's keys, the friends, the groups and,
  if wanted, the history, sealed with a passphrase the player chooses (PBKDF2, which both WebCrypto
  and OpenSSL have, with many rounds). Restoring it makes a new device of the same identity; the
  conversations with friends start anew by themselves.

### 2. Becoming friends

- **By link:** opening a friend link shows whose it is (name and picture, opened with the link
  key) and **Add as friend**. The request is the first message of a conversation (below) with each
  of that player's devices, dropped into their **request box** on the server.
- **In a session:** the menu of a player's tile gets **Add as friend**. The request goes over the
  session's own connection, peer to peer; the server is not needed for it.
- **A request** carries the sender's card, name, picture and a line of text. Accepting answers in
  the same conversation with the same, plus what the two need from then on: each other's **friends
  key** (for statuses, below) and a **write token** for each other's inboxes.
- **Making sure it is them:** each friendship has a **safety code** (from both fingerprints) that
  two friends can compare in person or in a session, and mark as checked. A friend whose identity
  keys change has to be confirmed again.
- **Against spam:** a request box takes requests only from somebody who knows the link (it is not
  listed anywhere), a few per hour from one address, and a few dozen in all; requests are dropped
  after two weeks. **New friend link** makes a new request box and link key: the old link stops
  working, the friends stay.

### 3. Status and activity

- **What a player shows:** Online, Away (no input for a while), Busy (no notifications), or
  Invisible (shows nothing, sees everybody). Offline is simply no fresh status.
- **What they do:** "Playing Gran Turismo 7 on PS5", "Hosting Friday racing (2 / 4 players)",
  "In Mara's room", "Just talking". The app knows all of it already (the console's game, the game
  detection on a computer, the room). And "Free tonight from 20:00", typed or picked.
- **Joining from the friends list:** a friend who hosts can let friends in with one click. Their
  status then carries the session's invite, sealed for friends only; **Join** works like a recent
  session. Setting: **Friends can join my sessions** (on or off); how they come in is the session's
  own rule (ask to join, as viewers, or to play).
- **How it travels:** each player keeps one status note on the server, sealed with their **friends
  key**, which every friend got over the private conversation. A status is out of date within three
  minutes anyway, so it needs no ratchet: the friends key moves on every day (a stolen day key reads
  that day and the days after, until the next key), and a new one goes to every friend at once when
  somebody is removed or blocked, and after a year at the latest. The note is kept under a name that
  only friends can work out, so a fingerprint alone does not tell when somebody is online. The app renews the note
  every minute; friends' notes are read in one request for all of them, every 30 seconds while the
  app runs (every 2 minutes in the tray), at once when the friends list opens.
- **Who sees it:** **Show my status** (to all friends, or to nobody), **Show what I play**
  on/off.

### 4. Messages and group chats

- **Private chats, the way Signal does them.** Between every device of one friend and every device
  of the other there is a conversation of its own:
  - It starts with **X3DH**: the first device fetches the other's card, signed prekey and one
    one-time prekey from the server, makes four key agreements (both identities, the signed prekey,
    the one-time prekey, a fresh key of its own) and from them the first key. The other device can
    answer when it is next online; the one-time prekey is gone from the server once used.
  - Then the **Double Ratchet**: every message has a key of its own from a chain that only moves
    forward (old keys cannot be worked out from new ones), and every answer brings a fresh key
    agreement, which heals a conversation after a stolen key. Keys of messages that arrive out of
    order are kept a while (at most 1000 per conversation, 30 days).
  - A message goes once to every device of the friend and to the writer's own other devices, so
    that everybody's devices show the whole chat; each copy is sealed in that pair's conversation.
- **Inboxes:** every device has an inbox on the server. It only takes notes with a friend's **write
  token** (and first messages in the request box). The device fetches, then takes the notes away.
  Undelivered notes stay up to 30 days.
- **Group chats, with Signal's Sender Keys.** Every member's device has a **sender key** for the
  group: a chain that moves forward with every message the device writes, and a key to sign with.
  It gives it to the other members' devices over the private conversations. A message is sealed
  once with the writer's next chain key, signed, and put into the group box once; every member can
  open it, nobody can open earlier ones from a later key. When somebody leaves, is removed or banned,
  and at the latest every week, every device makes a new sender key for the others: a removed member
  cannot read on, and a stolen key stops working. A note stays in the group box until every member's
  device has it, at most 30 days.
- **What a message can be:** text with emoji and links (with the preview the writer's app makes, as
  in sessions, sealed with the message so the others never open the link; a browser shows previews
  but makes none), a join card ("Mara invites you to Friday racing", with **Join**), a plan (below),
  and later pictures. A message can answer another one (a quote of it shows above, a click goes
  there), mention someone with `@` and their name (offered as soon as `@` is typed in a group; the
  mentioned player gets a notification even in a muted chat), carry reactions, and be deleted for
  everybody by its writer (a stand-in "Message deleted" stays). A chat or group can be muted for an
  hour, 8 hours or until turned back on, on each device by itself. In a browser, a closed page only
  gets an empty push ("something new from your friends"), so muting cannot keep that one quiet.
- **History** stays on the devices (the app: a file in its data folder; a browser: IndexedDB). The
  server keeps nothing it has delivered. A newly linked device gets the history from the device that
  links it, if the player wants; the backup file can carry it too.
- **Read and typing:** "seen" is kept per chat and told to the others in the next message or status;
  no typing indicators (they would cost a request each).
- **What the server still learns:** which inboxes and group boxes exist, how often and how much is
  written to them, from which address. Not who a box belongs to by name, not the words.

### 5. Agreeing on a time

- **Plans** in a chat or a group: "When?" with a few times to choose from; everybody marks yes,
  maybe or no; the chat shows which time suits most. Once one is picked: a reminder a quarter of an
  hour before, and for the host **Host now**, which opens the Host page with the group invited.
- **Free times** in the status ("Free tonight from 20:00") show in the friends list, sorted to the
  top when they overlap with one's own.

### 6. Moderation

- **Block somebody** (a friend, a stranger who sent a request, a player in a session): their write
  token for one's inboxes is withdrawn on the server, their requests are dropped, a new friends key
  goes to everybody else, and their messages in shared groups are hidden on one's own devices.
- **Groups:** the one who made a group and the admins they choose can **remove** members and **ban**
  them: everybody's sender keys are made anew without them, and the group box refuses the banned
  one's token. **Who can add people** is a group setting. Who is in a group is part of the group's
  own messages, signed by the admin who changed it, so that no server can add anybody.
- **Report:** since the server cannot read anything, a report sends the messages in question,
  opened, to the server's operator, and only with the reporter's say. The operator can **ban an
  identity** from their server: it then takes no requests, statuses or messages from it. A banned
  player can make a new identity, but nobody finds it without a new link, so a ban still keeps them
  away from the people who reported them. For the operator: a ban list file next to the PHP script.
- **Limits** on every box (sizes, counts, requests per address) so that one player cannot fill a
  server.

### 7. Tray, start with the computer, notifications (the app)

- **Tray icon** with the player's status as a dot; its menu: open the app, Online / Away / Busy /
  Invisible, the friends who host now (with Join), quit. **Closing the window keeps the app in the
  tray** is a setting.
- **Start with the computer** and **Start in the tray**, under Settings → General:
  - Windows: the user's `Run` key in the registry (`HKCU\...\CurrentVersion\Run`); the installer
    and the in-app update keep the path right.
  - macOS: a login item through `SMAppService` (macOS 13 and later), which the system lists under
    Login Items.
  - Linux: `~/.config/autostart/psremoteplaytogether.desktop`, pointing at the AppImage (whose
    path the app knows from `$APPIMAGE`).
- **Notifications** from the system (Qt's tray notifications: toasts on Windows, the notification
  centre on macOS, `org.freedesktop.Notifications` on Linux) for: a new message, a friend request,
  a friend who starts hosting (a choice per friend), a plan's reminder. A click opens that chat.
  Busy and a running game silence them; during a session they show on the picture as the app's
  own notices instead. A notification says who wrote, not what, unless the player chooses
  **Show the words in notifications**.

### 8. Web only

A player who never installs the app uses the browser page for everything:

- **The browser is a device like any other**, with its own prekeys and conversations. The page
  says plainly the first time that its keys live in this browser, and offers the backup file at
  once, because clearing the browser's data would lose them. **Link a device** works between
  browsers, and between a browser and the app, in both directions.
- **Friends, chats, statuses and plans** are the same as in the app, in the same places of the page
  (the start of the page next to the rooms, and in the stream menu during a session).
- **They cannot host**, which a browser cannot do, but they can join friends' rooms from the
  friends list, be invited from a chat, and show "In Mara's room".
- **Online only while the page is open.** A browser runs nothing for a closed page. Their status
  says "Online in a web browser" while a tab is open, and expires like the app's otherwise. What
  arrives meanwhile waits in the browser's inbox and is opened next time.
- **Notifications** while the page is closed need the browser's push messages, which only come to a
  page with a service worker. The page has none on purpose (it must always be the newest). The way
  out: a service worker that does **only** push and leaves loading the page alone, so the page stays
  fresh. The friends server sends an empty push ("something is new") through the browser's push
  service, signed with the server's own key (VAPID; plain PHP with OpenSSL can do that); the service
  worker fetches the inbox, opens what it can and shows the notification. The browser's push service
  learns nothing but that something is new. On an iPhone or iPad this works for the page installed
  on the home screen only (iOS 16.4 and later): the page says so.
- **The page on another server:** the page is on GitHub Pages, the friends server elsewhere; the
  server answers with the header that lets the page read it, as the meeting point does.
- **What a browser cannot promise:** keys in a web page are only as safe as the page that is
  loaded. Whoever could change the page's files could change its code. The page is served from the
  project's own GitHub repository with a strict Content-Security-Policy, and the app is the safer
  place for players who want the most; the page says so where it offers chats.

## The server script

`server/friends/psrpt_friends.php`, one file, a folder of files for its data (with file locks, as the
meeting point), settings as constants or environment variables, a README next to it for running it
behind a reverse proxy.

| What | Kept how long | Limit |
| --- | --- | --- |
| Cards (identity and devices) | while in use; a year without | 1 per identity, 10 devices |
| Signed prekeys | until replaced | 1 per device |
| One-time prekeys | until used | 100 per device |
| Link boxes (linking a device) | 2 minutes | a few per hour from one address |
| Request boxes | requests 14 days | 50 waiting, a few per hour from one address |
| Status notes | 3 minutes | 1 per identity, 2 KB |
| Inboxes (one per device) | 30 days undelivered | 1000 notes, 8 KB each |
| Group boxes | until all members' devices have them, at most 30 days | 2000 notes |
| Push subscriptions (web only) | until the browser drops them | 1 per browser |
| Ban list | kept | by the operator |

It answers the way the meeting point does (`text/plain` or small JSON), says how often to come back
("poll"), stores no addresses and writes no log of its own. The server checks signatures on cards
and prekeys, so that nobody but the identity can change them; it cannot open anything else. The
protocol gets its own document (`docs/friends-protocol.md`), with the exact key derivations, message
layouts and the test vectors both clients are checked against.

## What it costs a server

About one small request per device every 30 seconds while the app runs (every 2 minutes in the
tray), plus one per message and device it goes to. For a thousand players online with two devices
each: around 70 requests a second at most, mostly tiny. Raising "poll" is the server's way to slow
everybody down, as with the meeting point.

## Phases

1. **Identity and friends:** identities and devices, cards and prekeys, the private conversations
   (X3DH and the Double Ratchet, since requests and friends keys already travel in them), friend
   links, requests in the app and in sessions, the friends list with status, activity and **Join**,
   safety codes, linking and removing devices, the backup file; tray, start with the computer,
   notifications for requests and hosting friends. The same on the browser page.
2. **Chats:** private chats on the conversations from phase 1, group chats with sender keys, join
   cards, blocking, group removal and bans, history on the devices and handed to linked ones.
3. **Times and the rest:** plans and reminders, free times, push notifications for web only, reports
   and server bans.
4. **Later:** pictures in chats, voice messages.

Before phase 1 reaches players: the review of the protocol and of both clients' cryptography, and
the test vectors in `test/`, run by both the app's tests and the page's.

## Decided (2026-11-23)

- **End to end, with forward secrecy from the start**, Signal's way (X3DH, Double Ratchet, Sender
  Keys) with P-256.
- **No accounts:** keys and links, linked devices, a backup file.
- **History only on the devices**, handed on when a device is linked.
- **Web only in phase 1:** friends and status in the browser from the start, as in the app.
- **The default friends server on gamernova.net**, as the meeting point and the room list, with the
  setting to use another or none.
- **The review:** people from the open-source cryptography community (a paid review if there is a
  budget), and phase 1 first as a test with a few players.
