# Changelog

What changed in each release of PSRemotePlayTogether. The section of a version
becomes the "What's new" part of its release notes (`scripts/ci/release-notes.sh`).

## 0.3.1

- **Clearer choices for who gets in.** A public room now offers **Ask to join** (you approve each player first), **Join as viewers** (they play once you allow it) and **Join and play** (no approval, they play right away), each with a line that says what it means. The stream menu and the list of public rooms use the same words, so a room in the list shows *Ask to join*, *Join as viewer* or *Join and play*, and the filter is **Hide rooms that ask to join**. Before, "Everybody, playing at once" meant "right away" while **All at once** under Who controls meant "at the same time", and "Open" sat oddly next to **Open to newcomers**.
- **Who controls says what it does.** The choices are **Together**, **One at a time** and **Only me**, with a line under them on the Host page and in the stream menu: everybody's buttons count at the same time, whoever moves takes over while the others rest, or your friends watch. The list says *Everybody plays together*.
- **"Only me" shows everybody else as watching.** While only the host controls, the other players show as watching in the stream menu, on the game picture, in the OBS overlay and in the browser, with "only the host plays" in their menu. **Let them play** is dimmed until the host lets others control again, and the Host page says that everybody watches either way.

## 0.3.0

- **A new look.** The whole app is redesigned: the start page with its two doors, **Host** and **Join**, the Host and Join pages, the stream menu, the settings and every dialog. In the game, Play Together sits in a panel at the side (who knocks, who controls, the players with their webcams, your voice and webcam, the chat), with sound, picture, quality and overlays in a bar at the bottom. Everything works with a controller as well as with a mouse. The page for browsers looks the same.
- **One settings window.** The settings and the Play Together settings are one window with a sidebar (Playing, Play Together, Consoles, App) and your profile on top. A search field finds any setting on any page and lights it up there. Every setting is one row in plain words; choices are buttons you see at once, on and off are switches, amounts are sliders with their value. Keys are changed right on the page: click a key, press the new one. The meeting point and public rooms have real switches, and an address of your own comes back when you switch them on again. Each console has its own **Start right away** switch (was auto-connect). "Profiles" are now **Setting sets**, so they are not mixed up with your player profile. **Your profile** shows how the others see you.
- **Fixed: "Copy the address"** next to the renderer's documentation copied the PSN login address instead.
- **Fixed: no note for new chat messages.** While the chat was closed, a message from somebody else never showed on the game picture in the app, although the option for it was on. It does now, for a few seconds at the bottom left, as it already did in the browser.
- **Guests are no longer asked about the console's sleep.** Leaving a session asked every guest whether the console should go to sleep, and the answer did nothing: only the host can do that. Guests are not asked any more.
- **New: guests may put the console to sleep, if you say so.** **Guests may put the console to sleep when they leave** on the Host page, off by default. With it on, a guest who leaves is asked, in the app and in the browser, and "Sleep" ends the session for everybody. Waking the console stays the host's: a session only exists once the host's app is connected.
- **New: Host… and Join….** The start page has two buttons instead of forms spread over several pages. **Host…** sets the session up before it starts: private or public, how many friends, who controls, browsers, the console's sleep, "I play on the TV", and the invites to send. **Join…** takes an invite, or a room from the list of public rooms. **Play** on the console is for playing on your own; the app remembers which of the two you chose last, for when it connects at start.
- **New: public rooms.** Choose **Public** on the Host page, give the room a title and a language, and it is listed for everybody while it runs, with the game on the console and its picture, how many play, who controls (everybody at once, one at a time, only the host) and whether browsers are welcome. **Join… → Public rooms** searches the list by title, game, host and language, filters by language, full rooms and rooms that ask first, and sorts it. The page for browsers has the same list. The picture comes from Sony's public catalog, which the host's app asks about the game, without a login. The list only says how to find a room: joining goes straight to the host, and a public room has a key of its own, new for every public session. As with every invite, players connect to the host's computer directly and so learn its internet address; for a public room that can be anybody who reads the list.
- **New: a live picture in the list.** A public room shows what is being played right now: every 45 seconds the host's app takes one frame of the stream, makes a small picture of it (320×180) and sends it to the list, which shows it with a "Live" mark instead of the cover, in the app and on the page. **Live picture in the list** on the Host page switches it off; in streamer mode it is always off.
- **New: who plays, in the list.** Each public room shows its players as small circles, the host first: their own picture where they chose one, otherwise the first letter of their name, with how many play of how many. Only initials and pictures go to the list, never the other players' names.
- **New: you decide who gets in.** A public room lets each player in when the host says so, lets newcomers watch until the host lets them play, or is open to everybody. Who knocks shows as a note on the game picture and in the stream menu, with **Let in** and **No**; a guest at the door sees "Waiting for the host to let you in…". **Closed to newcomers** in the stream menu takes the room off the list and turns newcomers away, while players who were in before can come back. **Remove and block** keeps a player out for the rest of the session. A player keeps whether they may control when they come back.
- **One place for each thing.** Nothing to join or share is on two pages any more, and the settings only hold settings. Menus and lists open where they fit, never cut off at the edge of the window.
- **For servers of your own:** the list of public rooms is one small PHP file, like the meeting point (`server/rooms` in the source code). **Settings → Network & invites → Servers** takes its address, or switches public rooms off.
- **In the browser:** when a controller goes away on a phone or tablet, the controls on the screen come back.

## 0.2.0

- **Join from a web browser.** A friend without the app opens a link in Chrome, Edge, Firefox or Safari, plugs in a controller and plays along: same picture and sound, voice chat, text chat with link previews and emoji, webcams (their own too), a player picture, rumble. Nothing to install, no account. Without a controller the keyboard stands in, and phones and tablets get controls on the screen.
- **The browser looks and works like the app.** The same display on the game picture (players, webcams, everybody's controller, the console's controller, with the same presets and the same arranging by dragging), the same stream menu (Ctrl+O or ⌘O, L1+R1+L3+R3, or the Menu button), the same chat and the same options. The new **Web browser** invite is on the start page, under Play Together and in the stream menu.
- **Still no server in between.** The browser connects to the host's computer directly (WebRTC). The page only finds the host through the meeting point, the same way the app does without a forwarded port. The session's key stands behind the `#` of the link, which a browser sends to nobody, and the page takes it out of the address bar.
- **Browser guests are guests like the others.** They show in the player list with a small "WEB" mark and can be muted, set to watching and removed. The page comes back by itself after a lost connection.
- **Yours to switch off or host.** **Play Together → Friends can join from a web browser** switches it off. The page is a folder of static files (`web/`); **Page for browsers** takes the address of your own copy.
- **Good to know:** in a browser the game's haptics arrive as plain rumble, and it adds a little delay. Touchpad, motion sensors, adaptive triggers and the light bar need a DualSense that the browser opens directly (Chrome and Edge, "DualSense…" in the stream menu); that part is new and little tested. A PS5 streams H.265, which Chrome, Edge and Safari play on most computers; otherwise the host can switch to H.264. With most routers a browser gets in by itself, like the app. If one cannot, the host forwards **UDP port 9411** as well: browsers come in on a port of their own, one above the sharing port.
- **For meeting points of your own:** a web page can only use one that answers with `Access-Control-Allow-Origin`. The script can send it now (`PSRPT_MEET_CORS`), and its self-test says whether a browser could use it.
- **Fixed: a console that refuses was asked a hundred times a second.** When the first connection to a console failed (already in use, not awake yet), the app tried again every few milliseconds for 20 seconds. It now waits a second between tries, and cancelling stops them at once.
- **Fixed: `join` on the command line could end up at your own console.** With "connect at start" set and the console awake, a start that was meant to join a friend's session connected to the console first and the join was refused.
- **Fixed: controller colours on the game picture.** With a colour other than white, black or red, the controllers on the game picture had no sticks and showed no pressed shoulder buttons, directions or touchpad.

## 0.1.3

- **Join without a forwarded port.** With most routers the host no longer has to open a port. Host and guest find each other through a small meeting point on the internet and then connect directly ("hole punching"). Picture, sound and controllers never pass through a server. A forwarded port keeps working as before, and it is used by itself when it is there.
- **The host is told what to expect.** While you share, the stream menu says whether friends can get in without a forwarded port, or that your router is of the kind that still needs one (symmetric NAT).
- **Clearer messages when joining fails.** A guest is told whether the host was not found at all, or was found but the two routers did not let the connection through.
- **Invites survive a new address.** An Internet invite whose address is out of date, because the host's connection got a new one over night, still leads to the host through the meeting point.
- **Your own meeting point.** It is one small PHP file (`server/meeting-point` in the source code). Play Together has a field for its address, and "off" switches the meeting point off altogether.
- **IPv6.** A session now sends from the same IPv6 address that the IPv6 invite names, so a firewall rule made for that address also fits the answers.
- **Good to know:** hole punching does not get through every router. Mobile networks and some fibre connections still need the forwarded port or a mesh VPN.

## 0.1.2

- **Update check.** The app asks GitHub at start whether a newer version exists and tells you, with a link to the download. Nothing is downloaded by itself, and it can be switched off under Settings → General.
- **Support the project.** A Ko-fi button on the start page, for everybody who wants to buy the developer a coffee.
- **Now on GitHub.** The source code, the releases and the builds for Windows, Linux (x86_64 and arm64) and macOS all live on GitHub. Every release is built by GitHub Actions.
- **STUN server of your choice.** Play Together has a field for the server that is asked for your address on the internet. Empty means the default, Google's.
- **Linux arm64.** A real arm64 AppImage, built on an arm64 machine.
- **Fixed: a chat message that never arrived.** A message written at just the wrong moment was not sent again when its first packet got lost on the way. Now every message is repeated until it has arrived.

## 0.1.1

- **The display on the game picture shows on Windows and Linux.** Players, webcams and controllers are now drawn into the stream picture itself, so guests on Windows see them and can arrange them. This needs the Vulkan renderer, which is the default.
- **Controller only for the host.** Play on the TV and let the computer only send your controller and feed the overlays, for example for your stream in OBS. Friends who join still get picture and sound.
- **A proper "Controller only" screen** for guests who join without picture and sound, instead of a spinner that never ends.

## 0.1.0

- **First release.** Share a PS5 or PS4 Remote Play session with friends: everybody gets the picture and sound, and everybody's controller plays as controller 1.
- Voice chat, webcams and text chat for everybody in the session.
- The players, their webcams and their controllers on the game picture, arranged by dragging, with presets.
- A stream overlay for OBS with players, controllers, webcams and the conversation.
- Builds for Windows, Linux and macOS.
