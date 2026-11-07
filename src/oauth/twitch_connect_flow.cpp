#include "oauth/twitch_connect_flow.hpp"

#include "oauth/pkce.hpp"
#include "oauth/twitch_helix.hpp"
#include "oauth/twitch_oauth.hpp"

#include <QDesktopServices>
#include <QHostAddress>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <cstdint>
#include <utility>

namespace multistream {

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
		emit failed(QStringLiteral("Could not start the local login listener on any redirect port"));
		return;
	}

	const std::string challenge = code_challenge(code_verifier_);
	const std::string url = twitch_authorize_url(client_id_, active_redirect_uri_, state_, challenge);
	if (!QDesktopServices::openUrl(QUrl(QString::fromStdString(url)))) {
		listener_->close();
		emit failed(QStringLiteral("Could not open the browser for Twitch login"));
	}
}

void TwitchConnectFlow::refresh_with_token(std::string_view refresh_token)
{
	refresh_token_ = std::string(refresh_token);
	const std::string body = token_refresh_body(client_id_, refresh_token_);
	post_form(kTwitchTokenEndpoint, body, [this](std::optional<std::string> response) {
		if (!response) {
			emit failed(QStringLiteral("Could not reach Twitch"));
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
	connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
		pending_request_ += socket->readAll();
		const qsizetype line_end = pending_request_.indexOf("\r\n");
		if (line_end < 0) {
			return;
		}
		const QByteArray request_line = pending_request_.left(line_end);
		pending_request_.clear();

		const qsizetype first_space = request_line.indexOf(' ');
		const qsizetype second_space = request_line.indexOf(' ', first_space + 1);
		if (first_space < 0 || second_space < 0) {
			answer(socket, QStringLiteral("<h1>Bad request</h1>"));
			return;
		}
		const QByteArray target = request_line.mid(first_space + 1, second_space - first_space - 1);
		const qsizetype question = target.indexOf('?');
		const std::string query = question >= 0 ? std::string(target.constData() + question + 1,
								     target.size() - question - 1)
						       : std::string();

		listener_->close();
		answer(socket, QStringLiteral("<h1>You can close this tab</h1>"));
		handle_redirect_query(query);
	});
}

void TwitchConnectFlow::answer(QTcpSocket *socket, const QString &body)
{
	const QByteArray utf8 = body.toUtf8();
	QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: ";
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
			redirect.error_description.value_or(redirect.error.value_or("Twitch denied the login"));
		emit failed(QString::fromStdString(detail));
		return;
	}
	if (!redirect.state || *redirect.state != state_) {
		emit failed(QStringLiteral("The Twitch login did not match this request"));
		return;
	}
	if (!redirect.code) {
		emit failed(QStringLiteral("Twitch returned no login code"));
		return;
	}
	exchange_code(*redirect.code);
}

void TwitchConnectFlow::exchange_code(std::string_view code)
{
	const std::string body = token_exchange_body(client_id_, code, active_redirect_uri_, code_verifier_);
	post_form(kTwitchTokenEndpoint, body, [this](std::optional<std::string> response) {
		if (!response) {
			emit failed(QStringLiteral("Could not reach Twitch"));
			return;
		}
		const TwitchTokenResponse tokens = parse_token_response(*response);
		if (!tokens.ok()) {
			const std::string detail =
				tokens.error_description.value_or(tokens.error.value_or("authorization failed"));
			emit failed(QString::fromStdString(detail));
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
			emit failed(QStringLiteral("Could not reach Twitch"));
			return;
		}
		const std::optional<TwitchUser> user = parse_helix_users(*response);
		if (!user) {
			emit failed(QStringLiteral("Twitch did not return your account"));
			return;
		}
		channel_login_ = user->login;
		get_json(helix_stream_key_url(user->id), access_token_, [this](std::optional<std::string> key_response) {
			if (!key_response) {
				emit failed(QStringLiteral("Could not reach Twitch"));
				return;
			}
			const std::optional<std::string> key = parse_helix_stream_key(*key_response);
			if (!key) {
				emit failed(QStringLiteral("Twitch did not return a stream key"));
				return;
			}
			emit succeeded(QString::fromStdString(*key), QString::fromStdString(channel_login_),
				       QString::fromStdString(refresh_token_));
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
