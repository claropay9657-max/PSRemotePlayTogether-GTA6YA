// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef CHIAKI_APPUPDATE_H
#define CHIAKI_APPUPDATE_H

#include <string>
#include <vector>

/**
 * The app updating itself (appupdater.h), the parts of it that are only working out and
 * can be tried without a network, a file or another system: which file of a release is this
 * computer's, which addresses are followed, what a .sha256 file says, the script that swaps
 * the app's folder on Windows, and the changelog taken apart for showing. Plain C++, tried by
 * test/appupdate.cpp.
 */
namespace appupdate
{

/** The project's releases: nothing is ever downloaded from anywhere else. */
extern const char *const kReleaseFiles; // "https://github.com/Crypto90/PSRemotePlayTogether/releases/download/"

/**
 * The file of a release that is this computer's, as the releases name them
 * (scripts/ci/release-notes.sh). `system` is "macos", "windows" or "linux"; `cpu` as Qt names
 * the processor a build is for ("arm64", "x86_64"). "" where the releases have none.
 */
std::string AssetName(const std::string &version, const std::string &system, const std::string &cpu);

/**
 * Whether a file may be downloaded from this address: one of the project's own release files,
 * over https. `also` is another beginning that is allowed too, for trying the update against
 * a server of one's own ("" for none).
 */
bool TrustedUrl(const std::string &url, const std::string &also = std::string());

/**
 * What a .sha256 file (as sha256sum and shasum write it: the sum, then the file's name) says
 * the file `name` must hash to: 64 hex digits in small letters, or "" if it does not say.
 */
std::string Sha256For(const std::string &sums, const std::string &name);

/** A sum as the release's list of files gives it ("sha256:<hex>"), in small letters; "" if it is not one. */
std::string Sha256FromDigest(const std::string &digest);

/**
 * Where the new AppImage goes: in the old one's place, or, where the old one's name says its
 * version (as a downloaded one's does), next to it under the name of the new version.
 */
std::string AppImageTarget(const std::string &current_path, const std::string &old_version, const std::string &asset_name);

/**
 * Windows: the script (for cmd) that puts the new folder of the app in the old one's place
 * once the app has closed, starts the app again, and puts the old folder back if that could
 * not be done. A running program's folder cannot be replaced, so this happens after the app.
 *
 * The script is the same for every update and plain ASCII. What it works on it finds in its
 * environment, under the names below: a folder's name reaches it there as it is, whatever
 * letters it has and whatever a % or an & in it would mean to cmd anywhere else.
 */
std::string WindowsSwapScript();

/** The running app's process: the script waits until it is gone. */
extern const char *const kSwapPid; // "PSRPT_UPDATE_PID"
/** The folder the app is in. */
extern const char *const kSwapApp; // "PSRPT_UPDATE_APP"
/** The folder with the new version, in the same parent folder. */
extern const char *const kSwapNew; // "PSRPT_UPDATE_NEW"
/** What was unpacked for it (holds the new folder), removed at the end. */
extern const char *const kSwapStage; // "PSRPT_UPDATE_STAGE"
/** The program in the folder, started at the end. */
extern const char *const kSwapExe; // "PSRPT_UPDATE_EXE"

/** One thing that changed: what it is in a few words, and the rest. */
struct Change
{
	/** The words in bold at its start, without the "New:" some begin with. "" if it has none. */
	std::string title;
	/** What follows. Inline marks are still in it: **bold**, `code`. */
	std::string text;
	/** It began with "New:". */
	bool is_new = false;
	/** It is the note at the end ("Good to know:"). */
	bool is_note = false;
};

struct Version
{
	std::string version;
	std::vector<Change> changes;
};

/**
 * CHANGELOG.md taken apart: a version for each "## <version>" heading, newest first as the
 * file has them, each with its list. What stands before the first heading is left out.
 */
std::vector<Version> ParseChangelog(const std::string &markdown);

/**
 * A release's notes as GitHub has them (scripts/ci/release-notes.sh writes them): the list
 * under "## What's new", up to the next heading.
 */
std::vector<Change> ParseReleaseNotes(const std::string &markdown);

/**
 * A line of the changelog as rich text for showing: & < > made harmless, **bold** as <b>,
 * `code` as <code>. Nothing else of Markdown is understood, on purpose: the text comes from
 * the internet in the case of a release's notes, and is shown, not followed.
 */
std::string InlineHtml(const std::string &text);

} // namespace appupdate

#endif // CHIAKI_APPUPDATE_H
