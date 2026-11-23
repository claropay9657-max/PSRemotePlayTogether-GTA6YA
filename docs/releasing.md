# Building and releasing PSRemotePlayTogether

GitHub Actions builds the app for every platform. This page says what gets
built, how to publish a release, and how to reproduce a build on your own
machine.

## What is built

| Platform | Job in `.github/workflows/build.yml` | Result |
| --- | --- | --- |
| Linux x86_64 | "Linux AppImage (x86_64)" | `PSRemotePlayTogether-<version>-linux-x86_64.AppImage` |
| Linux arm64 | "Linux AppImage (aarch64)", on an arm64 runner | `PSRemotePlayTogether-<version>-linux-aarch64.AppImage` |
| Windows x64 | "Windows (cross-compiled)" | `PSRemotePlayTogether-<version>-windows-x64-setup.exe` (an installer, for the user alone) and `PSRemotePlayTogether-<version>-windows-x64.zip` (the same folder, unzip and run `PSRemotePlayTogether.exe`) |
| macOS (Apple silicon) | "macOS dmg (arm64)", on a macOS 15 runner | `PSRemotePlayTogether-<version>-macos-arm64.dmg` |

Every file comes with a `.sha256` next to it.

- **Every push to `main`** builds all four. The results can be downloaded for
  14 days from the run's page (Actions → the run → Artifacts). They are named
  `...-dev-<commit>-...`. A push that only changes documentation builds nothing.
- **Pushing a tag `v<version>`** builds the same and then publishes the release
  of that tag with all files attached, once every platform has built.

## Publishing a release

1. Set the version: `PSRPT_VERSION_*` in the top-level `CMakeLists.txt`.
2. Describe it: add a `## <version>` section at the top of `CHANGELOG.md`. That
   section becomes the "What's new" part of the release notes.
3. If QML changed, compile it with the Qt of the Windows and Linux builds (see
   "Checking the QML" below): those builds do not start the app's window, so nothing else
   notices QML that only a newer Qt understands.
4. Commit, and make sure the "Build" run of `main` is green.
5. Tag that commit and push the tag:

   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```

6. Wait for the "Build" run of the tag. Its last job, "Publish the release",
   creates the release with the notes and every file.
7. Post it on [r/PSRemotePlayTogether](https://www.reddit.com/r/PSRemotePlayTogether/),
   with the flair **Release**: every release is announced there.

A tag with a hyphen, such as `v1.0.0-rc1`, is published as a pre-release. The
app's update check only offers plain versions, so a pre-release is not
announced to users.

The version in the file names is the tag, and it has to be the version the app
shows in its corner. A tag that says something else than `CMakeLists.txt`
stops the build right away with a message saying so. (`CHIAKI_VERSION_*` next
to it is the version of the chiaki-ng the app is built on and only changes
when a newer chiaki-ng is merged in.)

If a job failed for a reason outside the code (a download that timed out, for
example), re-run it from the run's page. "Publish the release" can be re-run
too: files that are already attached are replaced, not duplicated.

### Checking the QML

A Mac with Homebrew builds against a newer Qt than the Windows and Linux builds (Qt 6.9.3,
`scripts/ci`). QML that only the newer one understands builds everywhere and then fails to
load on Windows and Linux: 0.4.0 to 0.4.2 closed right after starting for that reason.
`scripts/qmlcheck` compiles every QML file of the app with whatever Qt it is built against
and lists what does not compile:

```bash
QT=/path/to/Qt/6.9.3/macos   # for example from: aqt install-qt mac desktop 6.9.3 clang_64 --archives qtbase qtdeclarative qtsvg qtshadertools
cmake -S scripts/qmlcheck -B build/qmlcheck -G Ninja -DCMAKE_PREFIX_PATH=$QT && cmake --build build/qmlcheck
$QT/libexec/rcc -binary gui/src/qml/qml.qrc -o build/qmlcheck/qml.rcc
(cd gui/res && $QT/libexec/rcc -binary resources.qrc -o ../../build/qmlcheck/resources.rcc)
DYLD_FRAMEWORK_PATH=$QT/lib QT_PLUGIN_PATH=$QT/plugins QML_IMPORT_PATH=$QT/qml \
  build/qmlcheck/qmlcheck build/qmlcheck/qml.rcc build/qmlcheck/resources.rcc
```

It compiles and does not run: what a page reads from the app at run time is not checked.

### The release notes

`scripts/ci/release-notes.sh <version>` prints them: the version's section of
`CHANGELOG.md`, a table saying which file is for which system, the first-start
hints, the port a host has to forward, the Ko-fi link and "Powered by
GamerNova.net". Run it locally to see what a release will say.

### What users of older versions see

A moment after it starts, the app asks GitHub for the latest release
(`gui/src/qmlbackend_update.cpp`). If that is newer than the running version,
the start page shows "Update available" and asks whether to look at what is new.
Nothing is downloaded by itself, and **Settings → Updates & logs → Check for updates**
switches the check off. So a new release reaches existing users as soon as its
"Publish the release" job is done.

### The app updating itself

From 0.5.1 on, **Update and restart** on that page installs the release
(`gui/src/appupdater.cpp`; what can be tried without a network or another
system is in `gui/src/appupdate.cpp` and `test/appupdate.cpp`). For that a
release has to be what the build jobs make it:

- its files named as in the table above, each with its `.sha256` next to it.
  The app takes the file for its system, downloads it and the `.sha256`, and
  installs nothing whose SHA-256 is not the published one (GitHub's own
  `digest` of the file has to agree too, where the release's list has one);
- the Windows zip holding one folder with `PSRemotePlayTogether.exe` in it,
  the dmg holding `PSRemotePlayTogether.app`.

The app only follows addresses under
`https://github.com/Crypto90/PSRemotePlayTogether/releases/download/`. The sum
comes from the same release as the file: it catches a download that is broken
or cut short, not a release that was replaced by someone with access to the
repository. Signing releases with a key of the project's own would be the step
after this one.

How a version takes the old one's place:

| System | What happens |
| --- | --- |
| macOS | The app is copied out of the dmg next to the running bundle, the two are swapped by renaming, and the new one is opened once the old one has closed. Not possible while macOS runs the app from a protected copy (never moved since the download) or where the bundle cannot be changed. |
| Windows | The zip is unpacked next to the app's folder with Windows' own `tar.exe`. A script for `cmd` in the temp folder waits for the app to close, swaps the two folders, starts the app and removes the old folder; if the swap fails, the old folder comes back and is started. |
| Linux | The new AppImage replaces the one the app was started from (`$APPIMAGE`), under the new version's name if the old file's name said its version, and is started once the old one has closed. Builds that are no AppImage are not updated this way. |

Where the app cannot replace itself, the page says why and offers the download
page instead.

**Trying it without a release.** `PSRPT_UPDATE_CHECK_URL` makes the app ask
another address than GitHub's for the latest release. If that address is on
this computer (`127.0.0.1` or `localhost`), files it names there are followed
too. Serve a JSON file like GitHub's answer (`tag_name`, `body`, and `assets`
with `name` and `browser_download_url` for the file and its `.sha256`) from a
local web server, and start a copy of the app with the variable set: a copy,
because the update replaces the app it runs from.

## macOS

`scripts/release-macos.sh` builds the dmg, in CI and on your own Mac (its
header lists the Homebrew packages it needs). What it does:

- builds the app against Homebrew's Qt, FFmpeg, libplacebo and SDL
- copies every library the app needs into the bundle with `macdeployqt`, adds
  MoltenVK (Vulkan on macOS) and checks that nothing still points into Homebrew
- signs the bundle (with the project's own identity if there is one, see below,
  otherwise ad hoc), starts it once (`list`) to see that every library loads,
  and packs the dmg

The app bundles Homebrew's libraries, which only run on the macOS version they
were built for or newer. The CI runner is macOS 15, so the dmg needs macOS 15.
The app is not notarised: on the first start macOS blocks it, and the user
allows it under System Settings → Privacy & Security → Open Anyway.

### One identity for every version

macOS recognises an app by its signature. Signed ad hoc, every build is a new
app to it: after each update it asks again for screen recording and
accessibility, and System Settings lists the app once more under Local Network.
Signed by the same certificate every time, an update stays the same app.

A certificate of the project's own is enough for that (it does not need to come
from Apple; it does not remove the Open Anyway step, which only Apple's
notarisation does). Make it once, keep `sign.p12` and its password somewhere
safe, and never commit them:

```bash
openssl req -x509 -newkey rsa:3072 -sha256 -days 3650 -nodes \
  -keyout key.pem -out cert.pem -subj "/CN=PSRemotePlayTogether" \
  -addext "keyUsage=critical,digitalSignature" -addext "extendedKeyUsage=critical,codeSigning"
# -legacy: macOS' security tool cannot read OpenSSL 3's default encryption of a .p12
openssl pkcs12 -export -legacy -inkey key.pem -in cert.pem -name PSRemotePlayTogether -out sign.p12
base64 -i sign.p12 | pbcopy
```

Then, in the repository on GitHub, Settings → Secrets and variables → Actions:
`PSRPT_MAC_SIGN_P12` gets what was copied, `PSRPT_MAC_SIGN_PASSWORD` the
password given to `pkcs12`. The macOS job then signs with it and prints the
requirement macOS recognises the app by. The first version signed this way is
a new app to macOS one last time; from then on updates keep their permissions.
An Apple Developer ID certificate (with a paid Apple developer account) works
the same way and can later also be notarised.

## Reproducing a CI build locally

The Linux and Windows jobs only run one script inside a public container
image, so the same build works on any machine with Docker:

```bash
# Linux AppImage (about 5 minutes on 8 cores)
docker run --rm -v "$PWD:/work" -w /work docker.io/streetpea/chiaki-ng-builder:qt6.9.3 scripts/ci/build-linux.sh

# Windows zip (about 10 minutes the first time)
docker run --rm -v "$PWD:/work" -w /work debian:trixie scripts/ci/build-windows.sh
```

The results land in `dist/`. Both also work on an arm64 machine (an Apple
silicon Mac, for example); the Windows build is for x64 either way, the
AppImage is for the machine's own architecture.

## The page for web browsers

Friends can join from a web browser ([web-guests.md](web-guests.md)). The page
they open is the folder `web/`: static files, nothing to build.
`.github/workflows/pages.yml` publishes it to GitHub Pages at
`https://crypto90.github.io/PSRemotePlayTogether/`, the address the app's
**Web browser** invites lead to unless a host enters another one.

It is published whenever `web/` changes on `main` (and by hand: Actions → Web
page → Run workflow). Hosts run released versions, the page is always the
newest: as long as the protocol's version stays the same that is fine. A
change of the protocol version reaches browser guests as soon as it is on
`main`, and hosts on the release before it are then told to update when a
browser tries to join. So publish the release that brings such a change soon
after it is merged, or keep the change on a branch until then.

**Servers first.** The page and the app read the list of public rooms at
`https://gamernova.net/api/psrpt_rooms.php` (next to the meeting point, see
[server/rooms](../server/rooms/README.md)). Put that file on the server before
the page that reads it reaches `main`, or before the release that brings it:
otherwise the page and the app say the list could not be read, and nobody can
host a public room. The same goes for any later change to the list's requests.

This needs Pages switched on once: repository **Settings → Pages → Build and
deployment → Source: GitHub Actions**.

The WebRTC library the app needs for browser guests (libdatachannel) is
downloaded by CMake while configuring, at a pinned commit, and built into the
app. `-DCOPLAY_ENABLE_WEB=OFF` leaves it out, for a build without network.

## How the Windows build works

Windows is cross-compiled on Linux. `scripts/ci/setup-windows-cross.sh` puts
together, in `/opt/psrpt-win` inside the container:

| Part | Source |
| --- | --- |
| Compiler | [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) (clang, lld, mingw-w64 with UCRT) |
| Qt | the official Qt for Windows built with llvm-mingw, plus the same Qt version for Linux for its code generators (via `aqtinstall`) |
| libplacebo, SDL, Opus, OpenSSL and the other libraries | prebuilt packages from [MSYS2](https://www.msys2.org)'s CLANG64 repository, the same binaries a native MSYS2 build uses |
| FFmpeg | built from source with only the H.264/HEVC decoders the app uses |

`scripts/ci/build-windows.sh` then builds the app, and
`scripts/ci/deploy-windows.py` collects the Qt plugins, the QML modules and
every DLL the program needs into the zip (the job windeployqt does on Windows).
On an x86_64 runner the job finally starts the result with wine: it runs the
co-play tests and the app's `list` command, so a missing DLL fails the build.

Versions are pinned at the top of `setup-windows-cross.sh` (llvm-mingw, Qt,
FFmpeg). The MSYS2 packages are whatever is current when the job runs.

The zip is the app's folder as it is. The installer (`scripts/ci/windows-installer.nsi`,
built with NSIS' `makensis`, which runs on Linux) puts the same folder into
`%LOCALAPPDATA%\Programs\PSRemotePlayTogether`, with an entry in the Start menu and in
Windows' list of installed apps. It installs for the user alone and asks for no
administrator: under "Program Files" the app could not replace its own folder when it
updates itself. It is a 64-bit installer, so the build can try it with the 64-bit wine it
already has: installed without its windows, checked, and removed again.

The app's folder is the app's alone: an update from inside the app, a newer installer and
the uninstaller each replace or remove it as a whole. The installer therefore only takes a
folder that is new, empty or already holds the app. When the app updates itself, it takes
the uninstaller along into the new folder and says its new version in the list of installed
apps (`gui/src/appupdater.cpp`); the update itself always comes from the zip.

Neither file is code-signed, so Windows SmartScreen will ask before the first start.

## Webcam capture per platform

Sending your own webcam needs Qt Multimedia in the build. Showing the other
players' webcams, and voice chat, work everywhere.

| Package | Qt Multimedia | Own webcam |
| --- | --- | --- |
| Windows zip | yes (camera through Windows Media Foundation) | yes |
| macOS dmg | yes (Homebrew's `qtmultimedia`, if installed on the build Mac) | yes |
| Linux AppImage | no | no |

The Linux build image doesn't contain Qt Multimedia. Adding it means
installing the module into the image's Qt at build time, leaving out its
GStreamer plugin (the image lacks the GStreamer libraries it needs), and
either bundling PulseAudio's client library or requiring it on every system,
because Qt Multimedia links against it and the app's own sound would then go
through the same copy. That has to be tried on real Linux desktops first, so
it is left out for now; the app builds without it and simply has no webcam
switch.

## Licence

PSRemotePlayTogether is a fork of chiaki-ng and is licensed under the GNU
Affero General Public License v3 (`COPYING`). Giving the program to others,
including for free, is fine, but everyone who gets a build must also be able to
get the exact source code it was built from. This repository is that source:
every release is built from its tag, and GitHub attaches the source of the tag
to the release. Whoever offers the downloads somewhere else should link here.
The Windows zip contains the licence text as `LICENSE.txt`.
