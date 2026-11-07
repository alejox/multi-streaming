#include "oauth/twitch_connect_flow.hpp"

#include "oauth/pkce.hpp"
#include "oauth/twitch_helix.hpp"
#include "oauth/twitch_oauth.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDesktopServices>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <chrono>
#include <cstdint>
#include <memory>
#include <utility>

namespace multistream {

namespace {

// How long the browser login may take before the flow gives up and frees its port.
constexpr std::chrono::minutes kLoginTimeout{5};
// A request line longer than this is not the redirect; drop the connection.
constexpr qsizetype kMaxRequestHeadBytes = 16 * 1024;

QString module_text(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

} // namespace

TwitchConnectFlow::TwitchConnectFlow(std::string client_id, std::vector<std::string> redirect_uris, QObject *parent)
	: QObject(parent),
	  client_id_(std::move(client_id)),
	  redirect_uris_(std::move(redirect_uris))
{
	network_ = new QNetworkAccessManager(this);
}

void TwitchConnectFlow::start(const std::optional<std::string> &refresh_token)
{
	state_ = generate_code_verifier();
	if (refresh_token && !refresh_token->empty()) {
		refresh_with_token(*refresh_token);
	} else {
		start_browser_flow();
	}
}

bool TwitchConnectFlow::bind_listener()
{
	for (const std::string &uri : redirect_uris_) {
		const std::uint16_t port = static_cast<std::uint16_t>(QUrl(QString::fromStdString(uri)).port());
		if (port == 0) {
			continue;
		}
		QTcpServer *candidate = new QTcpServer(this);
		if (candidate->listen(QHostAddress::LocalHost, port)) {
			listener_ = candidate;
			active_redirect_uri_ = uri;
			active_redirect_path_ = redirect_uri_path(uri);
			connect(listener_, &QTcpServer::newConnection, this,
				[this] { handle_connection(listener_->nextPendingConnection()); });
			return true;
		}
		delete candidate;
	}
	return false;
}

void TwitchConnectFlow::start_browser_flow()
{
	code_verifier_ = generate_code_verifier();

	if (!bind_listener()) {
		fail(module_text("Twitch.ListenerFailed"));
		return;
	}

	login_timeout_ = new QTimer(this);
	login_timeout_->setSingleShot(true);
	connect(login_timeout_, &QTimer::timeout, this, [this] { fail(module_text("Twitch.Timeout")); });
	login_timeout_->start(kLoginTimeout);

	const std::string challenge = code_challenge(code_verifier_);
	const std::string url = twitch_authorize_url(client_id_, active_redirect_uri_, state_, challenge);
	if (!QDesktopServices::openUrl(QUrl(QString::fromStdString(url)))) {
		fail(module_text("Twitch.BrowserFailed"));
	}
}

void TwitchConnectFlow::stop_listening()
{
	if (login_timeout_) {
		login_timeout_->stop();
	}
	if (listener_) {
		listener_->close();
	}
}

void TwitchConnectFlow::fail(const QString &message)
{
	if (finished_) {
		return;
	}
	finished_ = true;
	stop_listening();
	emit failed(message);
}

void TwitchConnectFlow::succeed(const QString &stream_key)
{
	if (finished_) {
		return;
	}
	finished_ = true;
	stop_listening();
	emit succeeded(stream_key, QString::fromStdString(channel_login_), QString::fromStdString(refresh_token_));
}

void TwitchConnectFlow::refresh_with_token(std::string_view refresh_token)
{
	refresh_token_ = std::string(refresh_token);
	const std::string body = token_refresh_body(client_id_, refresh_token_);
	post_form(kTwitchTokenEndpoint, body, [this](std::optional<std::string> response) {
		if (!response) {
			fail(module_text("Twitch.Unreachable"));
			return;
		}
		const TwitchTokenResponse tokens = parse_token_response(*response);
		if (!tokens.ok()) {
			start_browser_flow();
			return;
		}
		access_token_ = tokens.access_token.value_or("");
		if (tokens.refresh_token) {
			refresh_token_ = *tokens.refresh_token;
		}
		fetch_user_and_key();
	});
}

void TwitchConnectFlow::handle_connection(QTcpSocket *socket)
{
	if (!socket) {
		return;
	}
	// Browsers also open speculative connections that never send a request, and ask for
	// /favicon.ico; each socket keeps its own buffer and frees itself once closed.
	connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
	auto buffer = std::make_shared<QByteArray>();
	connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] {
		*buffer += socket->readAll();
		const qsizetype line_end = buffer->indexOf("\r\n");
		if (line_end < 0) {
			if (buffer->size() > kMaxRequestHeadBytes) {
				socket->abort();
				socket->deleteLater();
			}
			return;
		}
		const QByteArray request_line = buffer->left(line_end);
		buffer->clear();
		handle_request_line(socket, request_line);
	});
}

void TwitchConnectFlow::handle_request_line(QTcpSocket *socket, const QByteArray &request_line)
{
	const std::optional<std::string> query = redirect_request_query(
		std::string_view(request_line.constData(), static_cast<std::size_t>(request_line.size())),
		active_redirect_path_);
	if (!query || finished_) {
		answer(socket, "404 Not Found",
		       QStringLiteral("<h1>%1</h1>").arg(module_text("Twitch.PageBadRequest").toHtmlEscaped()));
		return;
	}

	stop_listening();
	answer(socket, "200 OK", QStringLiteral("<h1>%1</h1>").arg(module_text("Twitch.PageDone").toHtmlEscaped()));
	handle_redirect_query(*query);
}

void TwitchConnectFlow::answer(QTcpSocket *socket, const char *status, const QString &body)
{
	const QByteArray utf8 = body.toUtf8();
	QByteArray response = "HTTP/1.1 ";
	response += status;
	response += "\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ";
	response += QByteArray::number(utf8.size());
	response += "\r\nConnection: close\r\n\r\n";
	response += utf8;
	socket->write(response);
	socket->disconnectFromHost();
}

void TwitchConnectFlow::handle_redirect_query(std::string_view query)
{
	const TwitchRedirect redirect = parse_redirect_query(query);
	if (redirect.error) {
		const std::string detail =
			redirect.error_description.value_or(redirect.error.value_or(obs_module_text("Twitch.Denied")));
		fail(QString::fromStdString(detail));
		return;
	}
	if (!redirect.state || *redirect.state != state_) {
		fail(module_text("Twitch.StateMismatch"));
		return;
	}
	if (!redirect.code) {
		fail(module_text("Twitch.NoCode"));
		return;
	}
	exchange_code(*redirect.code);
}

void TwitchConnectFlow::exchange_code(std::string_view code)
{
	const std::string body = token_exchange_body(client_id_, code, active_redirect_uri_, code_verifier_);
	post_form(kTwitchTokenEndpoint, body, [this](std::optional<std::string> response) {
		if (!response) {
			fail(module_text("Twitch.Unreachable"));
			return;
		}
		const TwitchTokenResponse tokens = parse_token_response(*response);
		if (!tokens.ok()) {
			const std::string detail =
				tokens.error_description.value_or(tokens.error.value_or(obs_module_text("Twitch.AuthFailed")));
			fail(QString::fromStdString(detail));
			return;
		}
		access_token_ = tokens.access_token.value_or("");
		refresh_token_ = tokens.refresh_token.value_or("");
		fetch_user_and_key();
	});
}

void TwitchConnectFlow::fetch_user_and_key()
{
	get_json(helix_users_url(), access_token_, [this](std::optional<std::string> response) {
		if (!response) {
			fail(module_text("Twitch.Unreachable"));
			return;
		}
		const std::optional<TwitchUser> user = parse_helix_users(*response);
		if (!user) {
			fail(module_text("Twitch.NoAccount"));
			return;
		}
		channel_login_ = user->login;
		get_json(helix_stream_key_url(user->id), access_token_, [this](std::optional<std::string> key_response) {
			if (!key_response) {
				fail(module_text("Twitch.Unreachable"));
				return;
			}
			const std::optional<std::string> key = parse_helix_stream_key(*key_response);
			if (!key) {
				fail(module_text("Twitch.NoStreamKey"));
				return;
			}
			succeed(QString::fromStdString(*key));
		});
	});
}

void TwitchConnectFlow::post_form(std::string_view url, std::string_view body, Handler on_done)
{
	QNetworkRequest request{QUrl(QString::fromStdString(std::string(url)))};
	request.setHeader(QNetworkRequest::ContentTypeHeader,
			  QStringLiteral("application/x-www-form-urlencoded"));
	QNetworkReply *reply =
		network_->post(request, QByteArray(body.data(), static_cast<qsizetype>(body.size())));
	finish_reply(reply, std::move(on_done));
}

void TwitchConnectFlow::get_json(std::string_view url, std::string_view access_token, Handler on_done)
{
	QNetworkRequest request{QUrl(QString::fromStdString(std::string(url)))};
	request.setRawHeader("Client-ID", QByteArray(client_id_.data(), static_cast<qsizetype>(client_id_.size())));
	request.setRawHeader("Authorization",
			     QByteArray("Bearer ") + QByteArray(access_token.data(), static_cast<qsizetype>(access_token.size())));
	QNetworkReply *reply = network_->get(request);
	finish_reply(reply, std::move(on_done));
}

void TwitchConnectFlow::finish_reply(QNetworkReply *reply, Handler on_done)
{
	connect(reply, &QNetworkReply::finished, this, [this, reply, on_done = std::move(on_done)] {
		const QByteArray data = reply->readAll();
		reply->deleteLater();
		if (reply->error() != QNetworkReply::NoError) {
			on_done(std::nullopt);
			return;
		}
		on_done(std::string(data.constData(), data.size()));
	});
}

} // namespace multistream
