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
#include "dock/destinations_dock.hpp"
#include "dock/dock_controller.hpp"
#include "dock/vertical_controller.hpp"
#include "dock/vertical_dock.hpp"
#include "persistence/file_config_repository.hpp"
#include "secrets/platform_secret_store.hpp"
#include "streaming/streaming_engine.hpp"
#include "vertical_canvas/vertical_canvas.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#if defined(MULTI_STREAM_E2E_SECRETS)
#include "e2e/e2e_commands.hpp"
#include "secrets/e2e_file_secret_store.hpp"
#endif

#include <QDockWidget>
#include <QMetaObject>
#include <QObject>
#include <QtCore/qglobal.h>

#include <memory>
#include <string>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

constexpr const char *kDockId = "obs-multi-stream-destinations";
constexpr const char *kVerticalDockId = "obs-multi-stream-vertical";

std::string module_config_path(const char *file)
{
	char *path = obs_module_config_path(file);
	const std::string result = path != nullptr ? path : "";
	bfree(path);
	return result;
}

// macOS minimizes a floating window on a double-click of its title bar. An OBS dock has no
// place in the macOS dock, so it just vanishes. Removing the minimize button also disables
// that double-click action, while the dock stays resizable and floatable.
void prevent_dock_minimize(QWidget *content)
{
	if (auto *dock_widget = qobject_cast<QDockWidget *>(content->parentWidget())) {
		dock_widget->setWindowFlag(Qt::WindowMinimizeButtonHint, false);
	}
}

std::unique_ptr<multistream::SecretStore> make_secret_store()
{
#if defined(MULTI_STREAM_E2E_SECRETS)
	obs_log(LOG_WARNING, "%s: stream keys come from a test file", multistream::E2eFileSecretStore::marker());
	return std::make_unique<multistream::E2eFileSecretStore>(module_config_path("e2e-secrets.txt"));
#else
	return multistream::make_platform_secret_store();
#endif
}

// Members are destroyed in reverse order: the controller stops listening, the
// Dispatcher's target drops queued work, and only then the engine goes.
struct Composition {
	explicit Composition(const std::string &config_path)
		: repository(config_path),
		  secrets(make_secret_store()),
		  vertical(module_config_path("vertical.json")),
		  engine(secrets, vertical, [this](std::function<void()> work) {
			  QMetaObject::invokeMethod(&dispatch_target, std::move(work), Qt::QueuedConnection);
		  })
	{
	}

	multistream::FileConfigRepository repository;
	std::shared_ptr<multistream::SecretStore> secrets;
	multistream::VerticalCanvas vertical;
	multistream::StreamingEngine engine;
	QObject dispatch_target;
	std::unique_ptr<multistream::DockController> controller;
	std::unique_ptr<multistream::VerticalController> vertical_controller;
#if defined(MULTI_STREAM_E2E_SECRETS)
	std::unique_ptr<multistream::E2eCommands> e2e_commands;
#endif
};

std::unique_ptr<Composition> composition;

void on_frontend_event(obs_frontend_event event, void *);

void tear_down()
{
	if (composition) {
		composition->engine.shutdown();
		composition->vertical_controller.reset();
		composition->vertical.detach();
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
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		composition->vertical.attach();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		// The canvas belongs to the collection that is about to be unloaded.
		composition->engine.stop_source_now(multistream::VideoSource::Vertical);
		composition->vertical.detach();
		break;
	case OBS_FRONTEND_EVENT_PROFILE_CHANGED:
		composition->vertical.refresh_video();
		break;
	case OBS_FRONTEND_EVENT_CANVAS_REMOVED:
		composition->vertical.check_removed();
		break;
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
		// Before OBS starts closing windows, which the preview's native view depends on.
		composition->vertical_controller.reset();
		composition->vertical.detach();
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		obs_frontend_remove_event_callback(on_frontend_event, nullptr);
		tear_down();
		break;
	default:
		break;
	}
}

void activate_features()
{
	const std::string path = module_config_path("destinations.json");
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

	// OBS owns the dock once it accepts it, and destroys it with the main window.
	auto *dock = new multistream::DestinationsDock();
	if (!obs_frontend_add_dock_by_id(kDockId, obs_module_text("MultiStream"), dock)) {
		obs_log(LOG_ERROR, "could not add the destinations dock");
		delete dock;
		dock = nullptr;
		composition->engine.set_destinations(std::move(load.config.destinations));
	} else {
		prevent_dock_minimize(dock);
		if (load.problem) {
			dock->show_problem(QString::fromStdString(*load.problem));
		}
		composition->controller = std::make_unique<multistream::DockController>(
			std::move(load.config), composition->repository, *composition->secrets, composition->engine,
			dock);
	}

	auto *vertical_dock = new multistream::VerticalDock();
	if (!obs_frontend_add_dock_by_id(kVerticalDockId, obs_module_text("Vertical.DockTitle"), vertical_dock)) {
		obs_log(LOG_ERROR, "could not add the vertical canvas dock");
		delete vertical_dock;
	} else {
		prevent_dock_minimize(vertical_dock);
		composition->vertical_controller = std::make_unique<multistream::VerticalController>(
			composition->vertical, composition->engine, vertical_dock);
	}

#if defined(MULTI_STREAM_E2E_SECRETS)
	composition->e2e_commands = std::make_unique<multistream::E2eCommands>(
		module_config_path("e2e-commands.txt"), composition->engine, composition->vertical, vertical_dock, dock,
		composition->vertical_controller);
#endif

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
