// Qt driver for the Twitch OAuth flow: opens the browser, catches the redirect on a local
// listener, exchanges the code, and fetches the stream key over libcurl on a worker thread. With a stored refresh
// token it refreshes instead and skips the browser. Emits exactly one terminal signal.
#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

class QTcpServer;
class QTcpSocket;
class QTimer;

namespace multistream {

struct HttpsRequest;

class TwitchConnectFlow final : public QObject {
	Q_OBJECT

public:
	TwitchConnectFlow(std::string client_id, std::vector<std::string> redirect_uris, QObject *parent = nullptr);
	// Cancels an HTTPS request in flight and waits (about a second at most) for its thread.
	~TwitchConnectFlow() override;

	// Pass a stored refresh token to try a silent refresh first; empty means full browser
	// flow. A failed refresh falls back to the browser flow automatically.
	void start(const std::optional<std::string> &refresh_token);

signals:
	void succeeded(const QString &stream_key, const QString &channel_login, const QString &refresh_token);
	void failed(const QString &message);

private:
	// Receives the response body, or nullopt when Twitch could not be reached.
	using Handler = std::function<void(std::optional<std::string>)>;

	bool bind_listener();
	void start_browser_flow();
	void refresh_with_token(std::string_view refresh_token);
	void handle_connection(QTcpSocket *socket);
	void handle_request_line(QTcpSocket *socket, const QByteArray &request_line);
	void answer(QTcpSocket *socket, const char *status, const QString &body);
	void stop_listening();
	void fail(const QString &message);
	void succeed(const QString &stream_key);
	void handle_redirect_query(std::string_view query);
	void exchange_code(std::string_view code);
	void fetch_user_and_key();

	void post_form(std::string_view url, std::string_view body, Handler on_done);
	void get_json(std::string_view url, std::string_view access_token, Handler on_done);
	void run_request(HttpsRequest request, Handler on_done);

	std::string client_id_;
	std::vector<std::string> redirect_uris_;
	std::string active_redirect_uri_;
	std::string active_redirect_path_;

	std::string state_;
	std::string code_verifier_;
	std::string refresh_token_;
	std::string access_token_;
	std::string channel_login_;

	// Shared with the worker thread, which outlives neither this object nor its destructor.
	std::shared_ptr<std::atomic<bool>> cancelled_ = std::make_shared<std::atomic<bool>>(false);
	std::thread worker_;
	QTcpServer *listener_ = nullptr;
	QTimer *login_timeout_ = nullptr;
	bool finished_ = false;
};

} // namespace multistream
