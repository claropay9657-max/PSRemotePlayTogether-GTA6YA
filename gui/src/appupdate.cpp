// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "appupdate.h"

#include <cctype>
#include <cstring>
#include <sstream>

namespace appupdate
{

const char *const kReleaseFiles = "https://github.com/Crypto90/PSRemotePlayTogether/releases/download/";

const char *const kSwapPid = "PSRPT_UPDATE_PID";
const char *const kSwapApp = "PSRPT_UPDATE_APP";
const char *const kSwapNew = "PSRPT_UPDATE_NEW";
const char *const kSwapStage = "PSRPT_UPDATE_STAGE";
const char *const kSwapExe = "PSRPT_UPDATE_EXE";

namespace
{

bool StartsWith(const std::string &text, const std::string &with)
{
	return text.size() >= with.size() && text.compare(0, with.size(), with) == 0;
}

std::string Trimmed(const std::string &text)
{
	size_t from = 0, to = text.size();
	while(from < to && std::isspace(static_cast<unsigned char>(text[from])))
		from++;
	while(to > from && std::isspace(static_cast<unsigned char>(text[to - 1])))
		to--;
	return text.substr(from, to - from);
}

std::string Lower(std::string text)
{
	for(char &c : text)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	return text;
}

bool IsSum(const std::string &text)
{
	if(text.size() != 64)
		return false;
	for(char c : text)
	{
		if(!std::isxdigit(static_cast<unsigned char>(c)))
			return false;
	}
	return true;
}

std::string FileName(const std::string &path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::vector<std::string> Lines(const std::string &text)
{
	std::vector<std::string> lines;
	std::istringstream in(text);
	std::string line;
	while(std::getline(in, line))
	{
		if(!line.empty() && line.back() == '\r')
			line.pop_back();
		lines.push_back(line);
	}
	return lines;
}

/** One "- …" of a list, taken apart. */
Change ChangeOf(const std::string &item)
{
	Change change;
	std::string text = Trimmed(item);
	if(StartsWith(text, "**"))
	{
		const size_t end = text.find("**", 2);
		if(end != std::string::npos)
		{
			change.title = text.substr(2, end - 2);
			text = text.substr(end + 2);
		}
	}
	const std::string is_new = "New: ";
	if(StartsWith(change.title, is_new))
	{
		change.is_new = true;
		change.title = change.title.substr(is_new.size());
		if(!change.title.empty())
			change.title[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(change.title[0])));
	}
	change.is_note = StartsWith(change.title, "Good to know");
	change.text = text;
	return change;
}

/** The list that starts at `from`, up to the next heading: each "- " an entry, what follows it without one belongs to it. */
std::vector<Change> ListFrom(const std::vector<std::string> &lines, size_t from, size_t *next)
{
	std::vector<Change> changes;
	std::string item;
	bool open = false;
	size_t i = from;
	for(; i < lines.size(); i++)
	{
		const std::string &line = lines[i];
		if(StartsWith(line, "## ") || StartsWith(line, "# "))
			break;
		if(StartsWith(line, "- "))
		{
			if(open)
				changes.push_back(ChangeOf(item));
			item = line.substr(2);
			open = true;
		}
		else if(open && !Trimmed(line).empty())
			item += " " + Trimmed(line);
		else if(open)
		{
			changes.push_back(ChangeOf(item));
			open = false;
		}
	}
	if(open)
		changes.push_back(ChangeOf(item));
	if(next)
		*next = i;
	return changes;
}

} // namespace

std::string AssetName(const std::string &version, const std::string &system, const std::string &cpu)
{
	if(version.empty())
		return std::string();
	// A plain version, nothing that could be more than a part of a file's name
	for(char c : version)
	{
		if(!std::isdigit(static_cast<unsigned char>(c)) && c != '.')
			return std::string();
	}
	const std::string start = "PSRemotePlayTogether-" + version + "-";
	if(system == "macos")
		return cpu == "arm64" ? start + "macos-arm64.dmg" : std::string();
	if(system == "windows")
		return cpu == "x86_64" ? start + "windows-x64.zip" : std::string();
	if(system == "linux")
	{
		if(cpu == "x86_64")
			return start + "linux-x86_64.AppImage";
		if(cpu == "arm64")
			return start + "linux-aarch64.AppImage";
	}
	return std::string();
}

bool TrustedUrl(const std::string &url, const std::string &also)
{
	// After the beginning only what a release's tag and a file's name are made of: no way out of the folder, nothing appended
	auto plain = [](const std::string &rest) {
		if(rest.empty() || rest.find("..") != std::string::npos)
			return false;
		for(char c : rest)
		{
			if(!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '_' && c != '/')
				return false;
		}
		return true;
	};
	if(StartsWith(url, kReleaseFiles))
		return plain(url.substr(std::strlen(kReleaseFiles)));
	if(!also.empty() && StartsWith(url, also))
		return plain(url.substr(also.size()));
	return false;
}

std::string Sha256For(const std::string &sums, const std::string &name)
{
	const std::vector<std::string> lines = Lines(sums);
	std::string only;
	int with_sum = 0;
	for(const std::string &raw : lines)
	{
		const std::string line = Trimmed(raw);
		if(line.size() < 64 || !IsSum(line.substr(0, 64)))
			continue;
		with_sum++;
		const std::string sum = Lower(line.substr(0, 64));
		std::string file = Trimmed(line.substr(64));
		// "*name" marks a file read as it is
		if(!file.empty() && file[0] == '*')
			file = file.substr(1);
		if(file.empty())
			only = sum;
		else if(FileName(file) == name)
			return sum;
	}
	// A sum with no name can only be this file's if it is the only one
	return with_sum == 1 ? only : std::string();
}

std::string Sha256FromDigest(const std::string &digest)
{
	const std::string kind = "sha256:";
	if(!StartsWith(digest, kind) || !IsSum(digest.substr(kind.size())))
		return std::string();
	return Lower(digest.substr(kind.size()));
}

std::string AppImageTarget(const std::string &current_path, const std::string &old_version, const std::string &asset_name)
{
	const size_t slash = current_path.find_last_of('/');
	const std::string dir = slash == std::string::npos ? std::string() : current_path.substr(0, slash + 1);
	const std::string name = slash == std::string::npos ? current_path : current_path.substr(slash + 1);
	if(!old_version.empty() && !asset_name.empty() && name.find(old_version) != std::string::npos)
		return dir + asset_name;
	return current_path;
}

std::string WindowsSwapScript()
{
	// Plain ASCII and the same for every update. The folders are in the environment
	// (PSRPT_UPDATE_...), where cmd takes a name as it is: no letter of it passes through a code
	// page, and a % or an & in it is not read as part of the script. All names the script sets
	// itself begin the same way, so the app that is started at the end finds nothing odd in its
	// environment. No ( ) blocks with variables that change inside them: cmd reads a block's
	// variables before it runs it.
	static const char *const lines[] = {
		"@echo off",
		"setlocal DisableDelayedExpansion EnableExtensions",
		"if \"%PSRPT_UPDATE_EXE%\"==\"\" goto end",
		"if \"%PSRPT_UPDATE_APP%\"==\"\" goto end",
		"set \"PSRPT_UPDATE_OLD=%PSRPT_UPDATE_APP%.old\"",
		// Windows' own tools, whatever else of those names is around
		"set \"PSRPT_UPDATE_SYS=%SystemRoot%\\System32\"",
		// Not in the app's folder: a folder that is a running program's working folder cannot be moved
		"cd /d \"%~dp0\"",
		"set PSRPT_UPDATE_WAITED=0",
		"set PSRPT_UPDATE_TRIES=0",
		// Until the app is gone; an app that does not close within ten minutes keeps what it has
		":wait",
		"\"%PSRPT_UPDATE_SYS%\\tasklist.exe\" /FI \"PID eq %PSRPT_UPDATE_PID%\" /NH 2>nul | \"%PSRPT_UPDATE_SYS%\\find.exe\" \" %PSRPT_UPDATE_PID% \" >nul",
		"if errorlevel 1 goto gone",
		"set /a PSRPT_UPDATE_WAITED+=1",
		"if %PSRPT_UPDATE_WAITED% geq 600 goto giveup",
		"\"%PSRPT_UPDATE_SYS%\\ping.exe\" -n 2 127.0.0.1 >nul",
		"goto wait",
		":gone",
		"if exist \"%PSRPT_UPDATE_OLD%\" rmdir /s /q \"%PSRPT_UPDATE_OLD%\"",
		// The old folder aside; something may still hold it for a moment. What tells is whether
		// the folder is where it should be, not what move says of itself.
		":aside",
		"move \"%PSRPT_UPDATE_APP%\" \"%PSRPT_UPDATE_OLD%\" >nul 2>&1",
		"if not exist \"%PSRPT_UPDATE_APP%\\\" goto swap",
		"set /a PSRPT_UPDATE_TRIES+=1",
		"if %PSRPT_UPDATE_TRIES% geq 30 goto failed",
		"\"%PSRPT_UPDATE_SYS%\\ping.exe\" -n 2 127.0.0.1 >nul",
		"goto aside",
		// The new folder in its place; a virus scanner may still be reading what was just unpacked
		":swap",
		"set PSRPT_UPDATE_TRIES=0",
		":place",
		"move \"%PSRPT_UPDATE_NEW%\" \"%PSRPT_UPDATE_APP%\" >nul 2>&1",
		"if exist \"%PSRPT_UPDATE_APP%\\%PSRPT_UPDATE_EXE%\" goto done",
		"set /a PSRPT_UPDATE_TRIES+=1",
		"if %PSRPT_UPDATE_TRIES% geq 15 goto back",
		"\"%PSRPT_UPDATE_SYS%\\ping.exe\" -n 2 127.0.0.1 >nul",
		"goto place",
		// It could not take its place: the old one back
		":back",
		"move \"%PSRPT_UPDATE_OLD%\" \"%PSRPT_UPDATE_APP%\" >nul 2>&1",
		"if exist \"%PSRPT_UPDATE_APP%\\%PSRPT_UPDATE_EXE%\" goto failed",
		"\"%PSRPT_UPDATE_SYS%\\ping.exe\" -n 3 127.0.0.1 >nul",
		"move \"%PSRPT_UPDATE_OLD%\" \"%PSRPT_UPDATE_APP%\" >nul 2>&1",
		"goto failed",
		":done",
		"start \"\" /d \"%PSRPT_UPDATE_APP%\" \"%PSRPT_UPDATE_APP%\\%PSRPT_UPDATE_EXE%\"",
		"rmdir /s /q \"%PSRPT_UPDATE_OLD%\" >nul 2>&1",
		"rmdir /s /q \"%PSRPT_UPDATE_STAGE%\" >nul 2>&1",
		"goto end",
		":failed",
		"rmdir /s /q \"%PSRPT_UPDATE_STAGE%\" >nul 2>&1",
		"if exist \"%PSRPT_UPDATE_APP%\\%PSRPT_UPDATE_EXE%\" start \"\" /d \"%PSRPT_UPDATE_APP%\" \"%PSRPT_UPDATE_APP%\\%PSRPT_UPDATE_EXE%\"",
		"goto end",
		":giveup",
		"rmdir /s /q \"%PSRPT_UPDATE_STAGE%\" >nul 2>&1",
		":end",
		// The script removes itself. cmd has read this whole line by then, so it does not look for the file again.
		"del \"%~f0\" >nul 2>&1 & exit",
	};
	std::string script;
	for(const char *line : lines)
	{
		script += line;
		script += "\r\n";
	}
	return script;
}

std::vector<Version> ParseChangelog(const std::string &markdown)
{
	std::vector<Version> versions;
	const std::vector<std::string> lines = Lines(markdown);
	for(size_t i = 0; i < lines.size();)
	{
		if(!StartsWith(lines[i], "## "))
		{
			i++;
			continue;
		}
		Version version;
		version.version = Trimmed(lines[i].substr(3));
		size_t next = i + 1;
		version.changes = ListFrom(lines, i + 1, &next);
		versions.push_back(version);
		i = next > i ? next : i + 1;
	}
	return versions;
}

std::vector<Change> ParseReleaseNotes(const std::string &markdown)
{
	const std::vector<std::string> lines = Lines(markdown);
	for(size_t i = 0; i < lines.size(); i++)
	{
		if(Trimmed(lines[i]) == "## What's new")
			return ListFrom(lines, i + 1, nullptr);
	}
	return {};
}

std::string InlineHtml(const std::string &text)
{
	std::string out;
	bool bold = false, code = false;
	for(size_t i = 0; i < text.size(); i++)
	{
		const char c = text[i];
		if(c == '&')
			out += "&amp;";
		else if(c == '<')
			out += "&lt;";
		else if(c == '>')
			out += "&gt;";
		else if(c == '`')
		{
			out += code ? "</code>" : "<code>";
			code = !code;
		}
		else if(!code && c == '*' && i + 1 < text.size() && text[i + 1] == '*')
		{
			out += bold ? "</b>" : "<b>";
			bold = !bold;
			i++;
		}
		else
			out += c;
	}
	// Whatever was left open ends with the line
	if(code)
		out += "</code>";
	if(bold)
		out += "</b>";
	return out;
}

} // namespace appupdate
