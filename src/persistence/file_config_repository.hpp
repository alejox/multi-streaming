// ConfigRepository backed by one JSON file, replaced atomically on save. A config
// that cannot be used is never overwritten: a corrupt file is moved aside and
// reported, and if that is impossible, saving stays refused. POSIX only.
#pragma once

#include "destinations/config_repository.hpp"

#include <filesystem>

namespace multistream {

class FileConfigRepository final : public ConfigRepository {
public:
	explicit FileConfigRepository(std::filesystem::path path) : path_(std::move(path)) {}

	ConfigLoad load() override;

	// Refused until load() has confirmed that nothing at the path would be lost.
	std::optional<std::string> save(const DestinationsConfig &config) override;

private:
	ConfigLoad refuse_saving(std::string problem);

	std::filesystem::path path_;
	bool safe_to_save_ = false;
};

} // namespace multistream
