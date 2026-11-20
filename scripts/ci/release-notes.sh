#!/bin/bash
# Prints the notes of a release as Markdown: what is new (the version's section
# of CHANGELOG.md), which file is for which system, and how to support the project.
#
#   scripts/ci/release-notes.sh 1.2.3 [FILE...]
#
# With files given, only those are listed under Downloads.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/../.."

VERSION="${1:?usage: $0 VERSION [FILE...]}"
shift || true
REPO="${GITHUB_REPOSITORY:-Crypto90/PSRemotePlayTogether}"
SERVER="${GITHUB_SERVER_URL:-https://github.com}"
BASE="$SERVER/$REPO/releases/download/v$VERSION"

# The lines between "## <version>" and the next "## "
changes="$(awk -v v="## ${VERSION%%-*}" '
	$0 == v { found = 1; next }
	found && /^## / { exit }
	found { print }
' CHANGELOG.md | sed -e '/./,$!d')"

have() {
	[ "$#" -eq 1 ] && return 0  # no file list given: list everything
	local wanted="$1" f
	shift
	for f in "$@"; do
		[ "$(basename "$f")" = "$wanted" ] && return 0
	done
	return 1
}

row() {
	local system="$1" file="$2" how="$3"
	shift 3
	if have "$file" "$@"; then
		echo "| $system | [\`$file\`]($BASE/$file) | $how |"
	fi
}

echo "**Play single-player PlayStation games together, from anywhere.** Everybody in the session controls the same player with their own controller, on one PS5 or PS4, with voice chat, webcams and an overlay for OBS."
echo
if [ -n "$changes" ]; then
	echo "## What's new"
	echo
	echo "$changes"
	echo
fi
echo "## Downloads"
echo
echo "| System | File | How to start |"
echo "| --- | --- | --- |"
row "Windows 10 / 11 (64-bit)" "PSRemotePlayTogether-$VERSION-windows-x64.zip" "Unzip, run \`PSRemotePlayTogether.exe\`" "$@"
row "macOS 15 or newer (Apple silicon)" "PSRemotePlayTogether-$VERSION-macos-arm64.dmg" "Open, drag the app to Applications" "$@"
row "Linux x86_64" "PSRemotePlayTogether-$VERSION-linux-x86_64.AppImage" "\`chmod +x\`, then run it" "$@"
row "Linux arm64" "PSRemotePlayTogether-$VERSION-linux-aarch64.AppImage" "\`chmod +x\`, then run it" "$@"
cat <<'NOTES'

Every file has a `.sha256` next to it. Everybody who uses the app needs the same version. A friend who only wants to join needs no download: the host can send a link that opens in a web browser.

The builds are not signed with a paid developer certificate. On macOS the first start is blocked: open **System Settings → Privacy & Security** and choose **Open Anyway**. On Windows, choose **More info → Run anyway** if SmartScreen asks.

**Hosting for friends over the internet:** with most routers there is nothing to set up, host and friends find each other by themselves. If a friend cannot connect, forward UDP port **9410** to your computer in your router (and **9411** for friends who join from a web browser). Friends in the same network or VPN need nothing, and guests open no ports.

---

## ☕ Support the project

If PSRemotePlayTogether gives you and your friends a good evening, please consider buying me a coffee:

[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20Me%20a%20Coffee-Donate-orange?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/crypto90)

Powered by [GamerNova.net](https://gamernova.net/) · built on [chiaki-ng](https://github.com/streetpea/chiaki-ng)
NOTES
