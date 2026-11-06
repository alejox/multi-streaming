#include "e2e/e2e_commands.hpp"

#include "streaming/streaming_engine.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <cstdio>
#include <fstream>
#include <sstream>

namespace multistream {

E2eCommands::E2eCommands(std::string path, StreamingEngine &engine) : path_(std::move(path)), engine_(engine)
{
	connect(&timer_, &QTimer::timeout, this, &E2eCommands::poll);
	timer_.start(100);
}

void E2eCommands::poll()
{
	std::ifstream file(path_);
	if (!file) {
		return;
	}

	std::string line;
	while (std::getline(file, line)) {
		std::istringstream fields(line);
		std::string command;
		std::string argument;
		fields >> command;
		std::getline(fields >> std::ws, argument);
		obs_log(LOG_INFO, "e2e command: %s %s", command.c_str(), argument.c_str());
		run(command, argument);
	}
	file.close();
	std::remove(path_.c_str());
}

void E2eCommands::run(const std::string &command, const std::string &argument)
{
	if (command == "start_stream") {
		obs_frontend_streaming_start();
	} else if (command == "stop_stream") {
		obs_frontend_streaming_stop();
	} else if (command == "start") {
		engine_.start({argument});
	} else if (command == "stop") {
		engine_.stop({argument});
	}
}

} // namespace multistream
