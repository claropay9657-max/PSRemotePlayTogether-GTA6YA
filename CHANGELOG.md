# Changelog

What changed in each release of PSRemotePlayTogether. The section of a version
becomes the "What's new" part of its release notes (`scripts/ci/release-notes.sh`).

## 0.1.2

- **Update check.** The app asks GitHub at start whether a newer version exists and tells you, with a link to the download. Nothing is downloaded by itself, and it can be switched off under Settings → General.
- **Support the project.** A Ko-fi button on the start page, for everybody who wants to buy the developer a coffee.
- **Now on GitHub.** The source code, the releases and the builds for Windows, Linux (x86_64 and arm64) and macOS all live on GitHub. Every release is built by GitHub Actions.
- **Linux arm64.** A real arm64 AppImage, built on an arm64 machine.

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
