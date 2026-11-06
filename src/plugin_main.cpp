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

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QtCore/qglobal.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	obs_log(LOG_INFO, "loaded version %s (built against libobs %d.%d.%d, running on OBS %s, Qt %s, frontend %s)",
		PLUGIN_VERSION, LIBOBS_API_MAJOR_VER, LIBOBS_API_MINOR_VER, LIBOBS_API_PATCH_VER,
		obs_get_version_string(), qVersion(), obs_frontend_get_main_window() ? "ready" : "pending");

	if (!multistream::is_runtime_supported(obs_get_version())) {
		obs_log(LOG_WARNING, "OBS %s is older than the required 33.0: features are disabled",
			obs_get_version_string());
	}

	return true;
}

void obs_module_unload(void)
{
	obs_log(LOG_INFO, "unloaded");
}
