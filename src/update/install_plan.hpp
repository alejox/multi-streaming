// The installer that runs after OBS exits, as a generated script: a POSIX shell script on
// macOS and Linux, a PowerShell script on Windows. It waits for OBS to exit, extracts the
// staged package, moves the current install aside, copies the new one in, and puts the old
// one back on any failure. Pure C++ so the quoting and the steps are tested; the Qt side only
// writes the script and launches it detached.
#pragma once

#include "update/update_check.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace multistream {

struct InstallPlan {
	UpdatePlatform platform = UpdatePlatform::MacosArm64;
	// The OBS process to wait for.
	long long obs_pid = 0;
	std::string version;
	// Absolute paths, in UTF-8.
	std::string install_root;
	std::string archive;
	// Removed when the installer finishes, whatever the outcome; must not contain the install.
	std::string staging_dir;
	// Receives one line: "ok <version> <detail>" or "failed <version> <detail>".
	std::string result_file;
};

// Single-quoted for POSIX sh, safe for any byte except NUL.
std::string posix_quote(std::string_view text);

// Single-quoted for PowerShell, where a quote is escaped by doubling it.
std::string powershell_quote(std::string_view text);

// The file name the script is written as: "install.sh" or "install.ps1".
std::string_view install_script_name(UpdatePlatform platform);

// nullopt when the plan is unsafe to run: a missing or relative path, control characters,
// an install root not named like the package root, overlapping staging and install
// directories, or no process to wait for.
std::optional<std::string> render_install_script(const InstallPlan &plan);

struct InstallResult {
	bool ok = false;
	std::string version;
	std::string detail;
};

// Reads the result file the script writes; tolerates a UTF-8 BOM and CRLF.
std::optional<InstallResult> parse_install_result(std::string_view text);

} // namespace multistream
