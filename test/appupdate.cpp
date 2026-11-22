// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
//
// The app updating itself, the parts that are only working out (gui/src/appupdate.cpp):
// which file is this computer's, which addresses are followed, what a .sha256 file says,
// the script for Windows, and the changelog taken apart.

#include "appupdate.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace appupdate;

static int failures = 0;
static int checks = 0;

#define CHECK(what) \
	do { \
		checks++; \
		if(!(what)) \
		{ \
			failures++; \
			std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #what); \
		} \
	} while(0)

static bool Has(const std::string &text, const std::string &part)
{
	return text.find(part) != std::string::npos;
}

static void Assets()
{
	CHECK(AssetName("0.5.1", "macos", "arm64") == "PSRemotePlayTogether-0.5.1-macos-arm64.dmg");
	CHECK(AssetName("0.5.1", "windows", "x86_64") == "PSRemotePlayTogether-0.5.1-windows-x64.zip");
	CHECK(AssetName("0.5.1", "linux", "x86_64") == "PSRemotePlayTogether-0.5.1-linux-x86_64.AppImage");
	CHECK(AssetName("1.10.0", "linux", "arm64") == "PSRemotePlayTogether-1.10.0-linux-aarch64.AppImage");
	// No build for these
	CHECK(AssetName("0.5.1", "macos", "x86_64").empty());
	CHECK(AssetName("0.5.1", "windows", "arm64").empty());
	CHECK(AssetName("0.5.1", "freebsd", "x86_64").empty());
	// A version is digits and dots, nothing else
	CHECK(AssetName("", "macos", "arm64").empty());
	CHECK(AssetName("0.5.1/../../x", "macos", "arm64").empty());
	CHECK(AssetName("0.5.1-rc1", "macos", "arm64").empty());
}

static void Addresses()
{
	const std::string base = "https://github.com/Crypto90/PSRemotePlayTogether/releases/download/";
	CHECK(TrustedUrl(base + "v0.5.1/PSRemotePlayTogether-0.5.1-macos-arm64.dmg"));
	CHECK(TrustedUrl(base + "v0.5.1/PSRemotePlayTogether-0.5.1-macos-arm64.dmg.sha256"));
	// Anything else is not followed
	CHECK(!TrustedUrl("http://github.com/Crypto90/PSRemotePlayTogether/releases/download/v0.5.1/a.dmg"));
	CHECK(!TrustedUrl("https://github.com/Somebody/PSRemotePlayTogether/releases/download/v0.5.1/a.dmg"));
	CHECK(!TrustedUrl("https://github.com.evil.example/Crypto90/PSRemotePlayTogether/releases/download/v0.5.1/a.dmg"));
	CHECK(!TrustedUrl("https://evil.example/?https://github.com/Crypto90/PSRemotePlayTogether/releases/download/v0.5.1/a.dmg"));
	CHECK(!TrustedUrl(base + "v0.5.1/../../../../Somebody/else/a.dmg"));
	CHECK(!TrustedUrl(base + "v0.5.1/a.dmg?x=1"));
	CHECK(!TrustedUrl(base + "v0.5.1/a.dmg#x"));
	CHECK(!TrustedUrl(base + "v0.5.1/a%2e%2e.dmg"));
	CHECK(!TrustedUrl(base));
	CHECK(!TrustedUrl(""));
	// A server of one's own, for trying, only when it is named
	CHECK(!TrustedUrl("http://127.0.0.1:8123/files/a.dmg"));
	CHECK(TrustedUrl("http://127.0.0.1:8123/files/a.dmg", "http://127.0.0.1:8123/"));
	CHECK(!TrustedUrl("http://127.0.0.1:8123/files/../a.dmg", "http://127.0.0.1:8123/"));
	CHECK(!TrustedUrl("http://127.0.0.1:9999/files/a.dmg", "http://127.0.0.1:8123/"));
}

static void Sums()
{
	const std::string sum = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
	const std::string big = "0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF0123456789ABCDEF";
	CHECK(Sha256For(sum + "  app.dmg\n", "app.dmg") == sum);
	CHECK(Sha256For(big + " *app.dmg\r\n", "app.dmg") == sum);
	CHECK(Sha256For(sum + "  dist/app.dmg\n", "app.dmg") == sum);
	CHECK(Sha256For(sum + "\n", "app.dmg") == sum);
	// Another file's sum is not this file's
	CHECK(Sha256For(sum + "  other.dmg\n", "app.dmg").empty());
	const std::string other = "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
	CHECK(Sha256For(other + "  other.dmg\n" + sum + "  app.dmg\n", "app.dmg") == sum);
	CHECK(Sha256For(other + "\n" + sum + "\n", "app.dmg").empty());
	CHECK(Sha256For("", "app.dmg").empty());
	CHECK(Sha256For("<html>not found</html>", "app.dmg").empty());
	CHECK(Sha256For(sum.substr(1) + "  app.dmg\n", "app.dmg").empty());
	CHECK(Sha256FromDigest("sha256:" + big) == sum);
	CHECK(Sha256FromDigest("sha512:" + sum).empty());
	CHECK(Sha256FromDigest("sha256:abc").empty());
	CHECK(Sha256FromDigest("").empty());
}

static void AppImages()
{
	const std::string asset = "PSRemotePlayTogether-0.5.1-linux-x86_64.AppImage";
	// As downloaded, the name says the version: the new one says the new one
	CHECK(AppImageTarget("/home/me/Downloads/PSRemotePlayTogether-0.5.0-linux-x86_64.AppImage", "0.5.0", asset) == "/home/me/Downloads/" + asset);
	// A name of the user's own stays: something may point to it
	CHECK(AppImageTarget("/home/me/Applications/psrpt.AppImage", "0.5.0", asset) == "/home/me/Applications/psrpt.AppImage");
	CHECK(AppImageTarget("psrpt.AppImage", "0.5.0", asset) == "psrpt.AppImage");
	CHECK(AppImageTarget("/home/me/psrpt-0.5.0.AppImage", "", asset) == "/home/me/psrpt-0.5.0.AppImage");
}

static void WindowsScript()
{
	const std::string script = WindowsSwapScript();
	// What it works on is in its environment, so the script itself is the same every time and plain ASCII
	for(char c : script)
		CHECK(static_cast<unsigned char>(c) < 128);
	const std::string pid = std::string("%") + kSwapPid + "%", app = std::string("%") + kSwapApp + "%",
		fresh = std::string("%") + kSwapNew + "%", stage = std::string("%") + kSwapStage + "%", exe = std::string("%") + kSwapExe + "%";
	const std::string old = "%PSRPT_UPDATE_OLD%";
	CHECK(Has(script, "set \"PSRPT_UPDATE_OLD=" + app + ".old\"\r\n"));
	CHECK(Has(script, "tasklist.exe\" /FI \"PID eq " + pid + "\" /NH"));
	CHECK(Has(script, "find.exe\" \" " + pid + " \""));
	// Nothing comes as an argument: cmd would take a % in a folder's name for a variable
	CHECK(!Has(script, "%1") && !Has(script, "%~1") && !Has(script, "%*"));
	// Every name of a file or folder stands in quotes
	for(const std::string &name : { app, fresh, stage, old })
	{
		for(size_t at = script.find(name); at != std::string::npos; at = script.find(name, at + 1))
			CHECK(at > 0 && (script[at - 1] == '"' || script[at - 1] == '='));
	}
	// It leaves the app's folder before it moves it, and uses Windows' own tools
	const size_t leave = script.find("cd /d \"%~dp0\"");
	// The old folder goes aside before the new one comes, and comes back if that fails
	const size_t aside = script.find("move \"" + app + "\" \"" + old + "\"");
	const size_t swap = script.find("move \"" + fresh + "\" \"" + app + "\"");
	const size_t back = script.find("move \"" + old + "\" \"" + app + "\"");
	const std::string starts = "start \"\" /d \"" + app + "\" \"" + app + "\\" + exe + "\"";
	const size_t start = script.find("\r\n" + starts);
	CHECK(leave != std::string::npos && aside != std::string::npos && swap != std::string::npos && back != std::string::npos && start != std::string::npos);
	CHECK(leave < aside && aside < swap && swap < back && back < start);
	CHECK(Has(script, "set \"PSRPT_UPDATE_SYS=%SystemRoot%\\System32\"\r\n"));
	CHECK(!Has(script, " tasklist ") && !Has(script, "| find ") && !Has(script, "\nping "));
	// Each step is judged by where the folders are, not by what move says of itself
	CHECK(Has(script, "if not exist \"" + app + "\\\" goto swap\r\n"));
	CHECK(Has(script, "if exist \"" + app + "\\" + exe + "\" goto done\r\n"));
	// The app is started again whether it worked or not (if it is there), but not while it still runs
	const size_t again = script.find("if exist \"" + app + "\\" + exe + "\" " + starts, start + 1);
	const size_t giveup = script.find(":giveup");
	CHECK(again != std::string::npos && giveup != std::string::npos && again < giveup);
	CHECK(script.find("start ", giveup) == std::string::npos);
	// Lines as Windows ends them, and no block with brackets around a variable that changes
	CHECK(!Has(script, ") else") && !Has(script, "(\r\n"));
	for(size_t i = 0; i < script.size(); i++)
	{
		if(script[i] == '\n')
			CHECK(i > 0 && script[i - 1] == '\r');
	}
	// Every label that is jumped to is there
	for(const char *label : { "wait", "gone", "aside", "swap", "place", "back", "done", "failed", "giveup", "end" })
	{
		CHECK(Has(script, std::string("\r\n:") + label + "\r\n"));
		CHECK(Has(script, std::string("goto ") + label + "\r\n"));
	}
	// And nothing is jumped to that is not there
	for(size_t at = script.find("goto "); at != std::string::npos; at = script.find("goto ", at + 1))
	{
		const size_t end = script.find("\r\n", at);
		CHECK(Has(script, "\r\n:" + script.substr(at + 5, end - at - 5) + "\r\n"));
	}
	// Every variable it reads is one it was given or has set itself
	for(size_t at = script.find("%PSRPT_UPDATE_"); at != std::string::npos; at = script.find("%PSRPT_UPDATE_", at + 1))
	{
		const size_t end = script.find('%', at + 1);
		const std::string name = script.substr(at + 1, end - at - 1);
		CHECK(name == kSwapPid || name == kSwapApp || name == kSwapNew || name == kSwapStage || name == kSwapExe
			|| Has(script, "set \"" + name + "=") || Has(script, "set " + name + "="));
	}
	// It ends by removing itself, on one line
	CHECK(Has(script, "\r\ndel \"%~f0\" >nul 2>&1 & exit\r\n"));
}

static void Changelog()
{
	const std::string text =
		"# Changelog\n"
		"\n"
		"What changed in each release.\n"
		"\n"
		"## 0.5.1\n"
		"\n"
		"- **New: play with mouse and keyboard.** Switch on **The mouse aims**.\n"
		"- **Every key can be changed**, up to three for each button.\n"
		"- A line with nothing in bold, and `code` in it.\n"
		"- **Good to know:** a console game reads a stick as how fast to turn.\n"
		"\n"
		"## 0.5.0\n"
		"\n"
		"- **New: the intro film.** What the app is for,\n"
		"  on a second line.\n"
		"\n"
		"## 0.4.3\n";
	const std::vector<Version> versions = ParseChangelog(text);
	CHECK(versions.size() == 3);
	if(versions.size() != 3)
		return;
	CHECK(versions[0].version == "0.5.1");
	CHECK(versions[0].changes.size() == 4);
	if(versions[0].changes.size() == 4)
	{
		const Change &first = versions[0].changes[0];
		CHECK(first.is_new && !first.is_note);
		CHECK(first.title == "Play with mouse and keyboard.");
		CHECK(first.text == " Switch on **The mouse aims**.");
		const Change &second = versions[0].changes[1];
		CHECK(!second.is_new && second.title == "Every key can be changed");
		CHECK(second.text == ", up to three for each button.");
		const Change &third = versions[0].changes[2];
		CHECK(third.title.empty() && third.text == "A line with nothing in bold, and `code` in it.");
		CHECK(versions[0].changes[3].is_note);
	}
	CHECK(versions[1].version == "0.5.0");
	CHECK(versions[1].changes.size() == 1);
	if(versions[1].changes.size() == 1)
		CHECK(versions[1].changes[0].text == " What the app is for, on a second line.");
	CHECK(versions[2].version == "0.4.3" && versions[2].changes.empty());
	CHECK(ParseChangelog("").empty());
	CHECK(ParseChangelog("no headings\n- an entry\n").empty());

	// A release's notes: only the list under "What's new"
	const std::string notes =
		"**Play your games together.**\n\n"
		"## What's new\n\n"
		"- **New: one.** Text.\n"
		"- Two.\n\n"
		"## Downloads\n\n"
		"| System | File |\n"
		"- not this\n";
	const std::vector<Change> changes = ParseReleaseNotes(notes);
	CHECK(changes.size() == 2);
	if(changes.size() == 2)
	{
		CHECK(changes[0].is_new && changes[0].title == "One.");
		CHECK(changes[1].title.empty() && changes[1].text == "Two.");
	}
	CHECK(ParseReleaseNotes("## Downloads\n- x\n").empty());
}

static void Html()
{
	CHECK(InlineHtml("plain") == "plain");
	CHECK(InlineHtml("a **b** c") == "a <b>b</b> c");
	CHECK(InlineHtml("run `share \"<x>\"` now") == "run <code>share \"&lt;x&gt;\"</code> now");
	// What comes from the internet stays text
	CHECK(InlineHtml("<img src=x onerror=y> & <a href='z'>") == "&lt;img src=x onerror=y&gt; &amp; &lt;a href='z'&gt;");
	CHECK(InlineHtml("**left open") == "<b>left open</b>");
	CHECK(InlineHtml("`a ** b`") == "<code>a ** b</code>");
	CHECK(InlineHtml("one * star") == "one * star");
}

int main()
{
	Assets();
	Addresses();
	Sums();
	AppImages();
	WindowsScript();
	Changelog();
	Html();
	std::printf("%d checks, %d failed\n", checks, failures);
	return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
