#include "update/https_download.hpp"

#include <plugin-support.h>

#include <curl/curl.h>

#include <fstream>
#include <memory>
#include <system_error>

namespace multistream {
namespace {

constexpr long kConnectTimeoutSeconds = 15;
// A stalled transfer, not a slow one, is what fails: below 1 KB/s for 30 seconds.
constexpr long kLowSpeedBytes = 1024;
constexpr long kLowSpeedSeconds = 30;
constexpr long kMaxRedirects = 5;

struct CurlDeleter {
	void operator()(CURL *handle) const { curl_easy_cleanup(handle); }
};

struct Sink {
	std::ofstream *file = nullptr;
	std::uint64_t written = 0;
	std::uint64_t max_bytes = 0;
	bool too_large = false;
};

std::size_t write_chunk(char *data, std::size_t size, std::size_t count, void *user)
{
	auto *sink = static_cast<Sink *>(user);
	const std::size_t bytes = size * count;
	if (sink->written + bytes > sink->max_bytes) {
		sink->too_large = true;
		return 0;
	}
	sink->file->write(data, static_cast<std::streamsize>(bytes));
	if (!*sink->file) {
		return 0;
	}
	sink->written += bytes;
	return bytes;
}

int check_cancelled(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t)
{
	return static_cast<const std::atomic<bool> *>(user)->load() ? 1 : 0;
}

} // namespace

DownloadResult download_to_file(const std::string &url, const std::filesystem::path &destination,
				std::uint64_t max_bytes, const std::atomic<bool> &cancelled)
{
	DownloadResult result;
	std::unique_ptr<CURL, CurlDeleter> curl(curl_easy_init());
	if (!curl) {
		result.error = "curl_easy_init failed";
		return result;
	}

	std::ofstream file(destination, std::ios::binary | std::ios::trunc);
	if (!file) {
		result.error = "cannot open the download file";
		return result;
	}

	Sink sink;
	sink.file = &file;
	sink.max_bytes = max_bytes;
	const std::string user_agent = std::string("obs-multi-stream-updater/") + PLUGIN_VERSION;
	char error_buffer[CURL_ERROR_SIZE] = {};
	CURL *handle = curl.get();

	curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
	curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "https");
	curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, "https");
#endif
	curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(handle, CURLOPT_MAXREDIRS, kMaxRedirects);
	curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(handle, CURLOPT_USERAGENT, user_agent.c_str());
	curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, error_buffer);
	curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_chunk);
	curl_easy_setopt(handle, CURLOPT_WRITEDATA, &sink);
	curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
	curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSeconds);
	curl_easy_setopt(handle, CURLOPT_LOW_SPEED_LIMIT, kLowSpeedBytes);
	curl_easy_setopt(handle, CURLOPT_LOW_SPEED_TIME, kLowSpeedSeconds);
	curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, check_cancelled);
	curl_easy_setopt(handle, CURLOPT_XFERINFODATA, const_cast<std::atomic<bool> *>(&cancelled));
#if defined(_WIN32) && defined(CURLSSLOPT_REVOKE_BEST_EFFORT)
	curl_easy_setopt(handle, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT));
#endif

	const CURLcode code = curl_easy_perform(handle);
	file.close();
	if (code != CURLE_OK || !file) {
		if (sink.too_large) {
			result.error = "the download is larger than expected";
		} else if (code != CURLE_OK) {
			result.error = error_buffer[0] != '\0' ? error_buffer : curl_easy_strerror(code);
		} else {
			result.error = "could not write the download file";
		}
		std::error_code ignored;
		std::filesystem::remove(destination, ignored);
		return result;
	}
	result.ok = true;
	result.bytes = sink.written;
	return result;
}

} // namespace multistream
