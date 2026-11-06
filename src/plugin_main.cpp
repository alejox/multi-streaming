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
#include "streaming/streaming_engine.hpp"
#include "streaming/video_sources.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QMetaObject>
#include <QObject>
#include <QtCore/qglobal.h>

#include <memory>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

// Members are destroyed in reverse order: the Dispatcher's target goes first and drops
// queued work, then the engine, which must not run that work anymore.
struct Composition {
	explicit Composition(const std::string &config_path)
		: repository(config_path),
		  secrets(multistream::make_platform_secret_store()),
		  engine(*secrets, video_sources, [this](std::function<void()> work) {
			  QMetaObject::invokeMethod(&dispatch_target, std::move(work), Qt::QueuedConnection);
		  })
	{
	}

	multistream::FileConfigRepository repository;
	std::unique_ptr<multistream::SecretStore> secrets;
	multistream::MainMixVideoSources video_sources;
	multistream::StreamingEngine engine;
	QObject dispatch_target;
};

std::unique_ptr<Composition> composition;

void on_frontend_event(obs_frontend_event event, void *);

void tear_down()
{
	if (composition) {
		composition->engine.shutdown();
		composition.reset();
	}
}

// OBS closes in this order: SCRIPTING_SHUTDOWN, scene data is cleared, EXIT, the frontend
// API is destroyed, and obs_module_unload much later. So outputs stop at the first event
// and the callback is removed at the last one, the final moment the API still exists.
void on_frontend_event(obs_frontend_event event, void *)
{
	if (!composition) {
		return;
	}

	switch (event) {
	case OBS_FRONTEND_EVENT_STREAMING_STARTING:
		composition->engine.main_stream_starting();
		break;
	case OBS_FRONTEND_EVENT_STREAMING_STOPPING:
	// A main stream that fails to start skips STOPPING.
	case OBS_FRONTEND_EVENT_STREAMING_STOPPED:
		composition->engine.main_stream_stopping();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGING:
		// The new profile resets video, which libobs refuses while outputs are active.
		// Nothing restarts afterwards: the user decides under the new profile.
		composition->engine.stop_everything_now();
		break;
	case OBS_FRONTEND_EVENT_SCRIPTING_SHUTDOWN:
		composition->engine.shutdown();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		obs_frontend_remove_event_callback(on_frontend_event, nullptr);
		tear_down();
		break;
	default:
		break;
	}
}

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

	composition = std::make_unique<Composition>(path);

	multistream::ConfigLoad load = composition->repository.load();
	if (load.problem) {
		obs_log(LOG_ERROR, "%s", load.problem->c_str());
	}
	obs_log(LOG_INFO, "loaded %zu destinations", load.config.destinations.size());
	composition->engine.set_destinations(std::move(load.config.destinations));

	obs_frontend_add_event_callback(on_frontend_event, nullptr);
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
	// Normally empty by now, because EXIT already tore everything down. The frontend
	// API is gone at this point, so the event callback cannot be removed from here.
	tear_down();
	obs_log(LOG_INFO, "unloaded");
}
