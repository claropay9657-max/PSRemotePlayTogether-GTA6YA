# Changelog

What changed in each release of PSRemotePlayTogether. The section of a version
becomes the "What's new" part of its release notes (`scripts/ci/release-notes.sh`).

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
