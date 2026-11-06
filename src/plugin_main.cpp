/*
obs-multi-stream
Copyright (C) 2026 alejox

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

// Composition root: wires the core to its OBS, Qt, and Keychain adapters.

#include "destinations/runtime_requirements.hpp"
#include "persistence/file_config_repository.hpp"
#include "secrets/platform_secret_store.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QtCore/qglobal.h>

#include <memory>
#include <optional>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

struct Composition {
	multistream::FileConfigRepository repository;
	std::unique_ptr<multistream::SecretStore> secrets;
	multistream::DestinationsConfig config;
};

std::optional<Composition> composition;

std::string config_file_path()
{
	char *path = obs_module_config_path("destinations.json");
	const std::string result = path != nullptr ? path : "";
	bfree(path);
	return result;
}

void activate_features()
{
	const std::string path = config_file_path();
	if (path.empty()) {
		obs_log(LOG_ERROR, "could not resolve the config path: features are disabled");
		return;
	}

	composition.emplace(Composition{multistream::FileConfigRepository(path),
					multistream::make_platform_secret_store(), {}});

	multistream::ConfigLoad load = composition->repository.load();
	if (load.problem) {
		obs_log(LOG_ERROR, "%s", load.problem->c_str());
	}
	composition->config = std::move(load.config);
	obs_log(LOG_INFO, "loaded %zu destinations", composition->config.destinations.size());
}

} // namespace

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "loaded version %s (built against libobs %d.%d.%d, running on OBS %s, Qt %s, frontend %s)",
		PLUGIN_VERSION, LIBOBS_API_MAJOR_VER, LIBOBS_API_MINOR_VER, LIBOBS_API_PATCH_VER,
		obs_get_version_string(), qVersion(), obs_frontend_get_main_window() ? "ready" : "pending");

	if (!multistream::is_runtime_supported(obs_get_version())) {
		obs_log(LOG_WARNING, "OBS %s is older than the required 33.0: features are disabled",
			obs_get_version_string());
		return true;
	}

	activate_features();
	return true;
}

void obs_module_unload(void)
{
	composition.reset();
	obs_log(LOG_INFO, "unloaded");
}
