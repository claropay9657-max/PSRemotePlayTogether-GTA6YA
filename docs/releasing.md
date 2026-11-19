# Building and releasing PSRemotePlayTogether

The Forgejo repository builds the app itself. This page says what gets built,
how to publish a release, and how to reproduce a build on your own machine.

## What is built

| Platform | Built by | Result |
| --- | --- | --- |
| Linux x86_64 | CI job "Linux AppImage (x86_64)" | `PSRemotePlayTogether-<version>-linux-x86_64.AppImage` |
| Linux arm64 | CI job "Linux AppImage (aarch64)" | `PSRemotePlayTogether-<version>-linux-aarch64.AppImage` |
| Windows x64 | CI job "Windows (cross-compiled)" | `PSRemotePlayTogether-<version>-windows-x64.zip` (portable, unzip and run `PSRemotePlayTogether.exe`) |
| macOS (Apple silicon) | `scripts/release-macos.sh` on a Mac | `PSRemotePlayTogether-<version>-macos-arm64.dmg` |

Every file comes with a `.sha256` next to it.

The workflows are in `.forgejo/workflows/` and run on the runners with the
labels `docker` (x86_64: both Linux x86_64 and Windows) and `docker-arm`
(arm64: the arm64 AppImage). (The files under `.github/workflows/` belong to upstream chiaki-ng
and are not used here: Forgejo only reads `.forgejo/workflows/` when that
folder exists.)

- **Every push to `main`** builds Linux and Windows. The results can be
  downloaded for 14 days from the run's page (Actions → the run → Artifacts).
  They are named `...-dev-<commit>-...`.
- **Pushing a tag `v<version>`** builds the same and attaches the files to the
  release of that tag, creating the release if it doesn't exist yet.

## Publishing a release

1. Make sure `main` is what you want to release and its last CI run is green.
2. Tag it and push the tag:

   ```bash
   git tag v1.0.0
   git push origin v1.0.0
   ```

3. Wait for the "Build" run of the tag. The release then appears under
   Releases with the AppImages and the Windows zip attached.
4. Add the macOS build from a Mac (see below).
5. Edit the release text on the Forgejo page if you want more than the default
   "Built from commit …".

A tag with a hyphen, such as `v1.0.0-rc1`, is published as a pre-release.

If a job failed for a reason outside the code (a download that timed out, for
example), re-run it from the run's page: files that are already attached are
replaced, not duplicated.

The version in the file names is the tag, and it has to be the version the app
shows in its corner: `PSRPT_VERSION_*` in the top-level `CMakeLists.txt`. So a
release is two steps: set those numbers and commit, then tag that commit
`v<the same version>`. A tag that says something else than the app stops the
build right away with a message saying so. (`CHIAKI_VERSION_*` next to it is
the version of the chiaki-ng the app is built on and only changes when a newer
chiaki-ng is merged in.)

## macOS

macOS programs can only be built on a Mac, and the CI runner is Linux. There
are two ways to get the `.dmg`.

### By hand

On a Mac with [Homebrew](https://brew.sh):

```bash
brew install cmake ninja pkgconf nasm qtbase qtdeclarative qtsvg qtmultimedia ffmpeg libplacebo sdl2-compat opus openssl@3 protobuf@29 speexdsp json-c miniupnpc libevent
git fetch --tags && git checkout v1.0.0
git submodule update --init --recursive test/munit third-party
scripts/release-macos.sh
```

The dmg is written to `dist/`. To attach it to the tag's release in one go,
create an access token in Forgejo (Settings → Applications, with read and write
access to repositories) and run:

```bash
FORGEJO_TOKEN=<token> scripts/release-macos.sh --upload
```

Good to know:

- The app bundles Homebrew's libraries, and those only run on the macOS
  version they were installed on or newer. A dmg built on macOS 15 needs
  macOS 15. Build on the oldest macOS you want to support.
- The app is only ad-hoc signed, not notarised by Apple. On another Mac the
  first start has to be allowed by hand (right-click the app → Open, or
  System Settings → Privacy & Security → Open Anyway). Removing that step
  needs a paid Apple Developer ID.
- An Intel Mac produces `...-macos-x86_64.dmg` the same way.

### With a Mac as a runner

A Mac can also build the dmg as part of the Forgejo pipelines:

1. Install the tools from "By hand" above, plus `brew install node`.
2. Get the runner program for macOS from
   <https://code.forgejo.org/forgejo/runner/releases> (or build it with Go).
3. In Forgejo open the repository's Settings → Actions → Runners → "Create new
   runner" and copy the registration token.
4. Register and start it:

   ```bash
   forgejo-runner register --no-interactive --instance https://<your-forgejo> --token <registration token> --name mac --labels macos:host
   forgejo-runner daemon
   ```

   `macos:host` means jobs run directly on the Mac, not in a container.
5. Start the workflow "Build macOS" from the repository's Actions page
   ("Run workflow"), choosing the tag to build. It builds the dmg on the Mac
   and attaches it to that tag's release.

To have every tag built without that click, uncomment the `push:` lines at the
top of `.forgejo/workflows/macos.yml`. They are off by default because Forgejo
keeps a job waiting until a runner with its label takes it: on a server
without a Mac runner every tag would leave a run waiting forever.

## Reproducing a CI build locally

Each job only runs one script inside a public container image, so the same
build works on any machine with Docker:

```bash
# Linux AppImage (about 5 minutes on 8 cores)
docker run --rm -v "$PWD:/work" -w /work docker.io/streetpea/chiaki-ng-builder:qt6.9.3 scripts/ci/build-linux.sh

# Windows zip (about 10 minutes the first time)
docker run --rm -v "$PWD:/work" -w /work debian:trixie scripts/ci/build-windows.sh
```

The results land in `dist/`. Both also work on an arm64 machine (an Apple
silicon Mac, for example); the Windows build is for x64 either way, the
AppImage is for the machine's own architecture.

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

There is no Windows installer yet, only the portable zip. The app is not
code-signed, so Windows SmartScreen will ask before the first start.

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
get the exact source code it was built from. In practice: publish the source
of each released tag (a public repository, or a source archive next to the
downloads) and say where it is on the download page. The Windows zip contains
the licence text as `LICENSE.txt`.
