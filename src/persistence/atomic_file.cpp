#include "persistence/atomic_file.hpp"

#if defined(_WIN32)
#include <windows.h>

#include <algorithm>
#include <system_error>
#else
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#endif

#include <fstream>
#include <iterator>
#include <system_error>

namespace multistream {
namespace fs = std::filesystem;
namespace {

// Paths in messages are UTF-8, as OBS logs expect. Unlike string(), u8string() cannot throw
// on Windows for a path outside the system code page.
std::string display(const fs::path &path)
{
	return path.u8string();
}

#if defined(_WIN32)

std::string last_error_text()
{
	return std::system_category().message(static_cast<int>(GetLastError()));
}

bool write_and_flush(HANDLE file, const std::string &content)
{
	constexpr std::size_t kMaxChunk = 1u << 30;
	std::size_t written = 0;
	while (written < content.size()) {
		const std::size_t chunk = (std::min)(content.size() - written, kMaxChunk);
		DWORD count = 0;
		if (!WriteFile(file, content.data() + written, static_cast<DWORD>(chunk), &count, nullptr)) {
			return false;
		}
		written += count;
	}
	return FlushFileBuffers(file) != 0;
}

// CREATE_NEW plays the part of mkstemp. The file inherits the directory's permissions, and
// the per-user config directory is private to the user by default.
HANDLE create_temporary_file(const fs::path &path, fs::path &temp_path)
{
	HANDLE file = INVALID_HANDLE_VALUE;
	for (int attempt = 0; attempt < 1000; ++attempt) {
		temp_path = path;
		temp_path += L".tmp-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(attempt);
		file = CreateFileW(temp_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
				   nullptr);
		if (file != INVALID_HANDLE_VALUE || GetLastError() != ERROR_FILE_EXISTS) {
			break;
		}
	}
	return file;
}

// Atomically replaces the file with a flushed copy of the content. On failure the previous
// file is untouched. MOVEFILE_WRITE_THROUGH returns once the move is on disk, which makes a
// separate directory sync unnecessary.
std::optional<std::string> replace_file(const fs::path &path, const std::string &content)
{
	fs::path temp_path;
	HANDLE file = create_temporary_file(path, temp_path);
	if (file == INVALID_HANDLE_VALUE) {
		return "Could not create a temporary file next to " + display(path) + ": " + last_error_text();
	}

	const bool written = write_and_flush(file, content);
	const std::string write_error = written ? std::string() : last_error_text();
	CloseHandle(file);
	if (!written) {
		DeleteFileW(temp_path.c_str());
		return "Could not write " + display(temp_path) + ": " + write_error;
	}

	if (!MoveFileExW(temp_path.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
		const std::string move_error = last_error_text();
		DeleteFileW(temp_path.c_str());
		return "Could not replace " + display(path) + ": " + move_error;
	}
	return std::nullopt;
}

#else

std::string errno_text()
{
	return std::strerror(errno);
}

bool write_and_sync(int descriptor, const std::string &content)
{
	std::size_t written = 0;
	while (written < content.size()) {
		const ssize_t count = write(descriptor, content.data() + written, content.size() - written);
		if (count < 0) {
			if (errno == EINTR) {
				continue;
			}
			return false;
		}
		written += static_cast<std::size_t>(count);
	}
	return fsync(descriptor) == 0;
}

// Best effort: the rename already happened, this only makes it durable sooner.
void sync_directory(const fs::path &directory)
{
	const int descriptor = open(directory.c_str(), O_RDONLY);
	if (descriptor >= 0) {
		fsync(descriptor);
		close(descriptor);
	}
}

// Atomically replaces the file with a synced copy of the content. On failure the previous
// file is untouched.
std::optional<std::string> replace_file(const fs::path &path, const std::string &content)
{
	// mkstemp creates the file with mode 0600, and the rename keeps that mode.
	std::string temp_path = display(path) + ".tmp-XXXXXX";
	const int descriptor = mkstemp(temp_path.data());
	if (descriptor < 0) {
		return "Could not create a temporary file next to " + display(path) + ": " + errno_text();
	}

	const bool written = write_and_sync(descriptor, content);
	const std::string write_error = written ? std::string() : errno_text();
	close(descriptor);
	if (!written) {
		unlink(temp_path.c_str());
		return "Could not write " + temp_path + ": " + write_error;
	}

	if (std::rename(temp_path.c_str(), path.c_str()) != 0) {
		const std::string rename_error = errno_text();
		unlink(temp_path.c_str());
		return "Could not replace " + display(path) + ": " + rename_error;
	}

	sync_directory(path.parent_path());
	return std::nullopt;
}

#endif

} // namespace

fs::path backup_path(const fs::path &path)
{
	fs::path backup = path;
	backup += ".bak";
	return backup;
}

bool read_whole_file(const fs::path &path, std::string &content)
{
	std::ifstream file(path, std::ios::binary);
	if (!file) {
		return false;
	}
	content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	return !file.bad();
}

std::optional<std::string> replace_file_keeping_backup(const fs::path &path, const std::string &content)
{
	// The backup is a full synced copy rather than a rename of the current file, so the
	// target exists at every moment and a crash in between leaves both files usable.
	std::error_code error;
	if (fs::is_regular_file(path, error)) {
		std::string previous;
		if (!read_whole_file(path, previous)) {
			return "Could not read " + display(path) + " to back it up";
		}
		if (auto problem = replace_file(backup_path(path), previous)) {
			return problem;
		}
	}
	return replace_file(path, content);
}

} // namespace multistream
