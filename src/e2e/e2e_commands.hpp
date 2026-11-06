// Test hook for runtime tests with no UI access: polls a file for commands. It exists only
// in builds configured with MULTI_STREAM_E2E_SECRETS.
#pragma once

#if !defined(MULTI_STREAM_E2E_SECRETS)
#error "The command hook is only for builds configured with MULTI_STREAM_E2E_SECRETS."
#endif

#include <QObject>
#include <QPointer>
#include <QTimer>

#include <memory>
#include <string>

namespace multistream {

class StreamingEngine;
class VerticalCanvas;
class VerticalController;
class VerticalDock;
class DestinationsDock;

class E2eCommands final : public QObject {
	Q_OBJECT

public:
	E2eCommands(std::string path, StreamingEngine &engine, VerticalCanvas &canvas, VerticalDock *dock,
		    DestinationsDock *destinations_dock, const std::unique_ptr<VerticalController> &controller);

private:
	void poll();
	void run(const std::string &command, const std::string &argument);
	void dump_vertical();
	void dump_items();
	void preview_mouse(const std::string &argument);
	void preview_key(const std::string &argument);
	void grab(const std::string &argument);
	void answer_dialogs();
	void log_dock_widgets();

	std::string path_;
	StreamingEngine &engine_;
	VerticalCanvas &canvas_;
	VerticalDock *dock_;
	QPointer<DestinationsDock> destinations_dock_;
	const std::unique_ptr<VerticalController> &controller_;
	QTimer timer_;
	QTimer answer_timer_;
	bool auto_answer_ = false;
};

} // namespace multistream
