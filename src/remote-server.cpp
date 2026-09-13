#include "remote-server.hpp"

#include "script-url.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QRandomGenerator>

RemoteServer::RemoteServer(TeleprompterState *state, QObject *parent) : QObject(parent), state_(state)
{
	connect(&server_, &QTcpServer::newConnection, this, &RemoteServer::acceptClient);
	connect(&discoverySocket_, &QUdpSocket::readyRead, this, &RemoteServer::readDiscoveryDatagram);
	regenerateToken();
}

bool RemoteServer::start(quint16 port)
{
	if (server_.isListening())
		server_.close();
	const bool ok = server_.listen(QHostAddress::AnyIPv4, port);
	discoverySocket_.close();
	discoverySocket_.bind(QHostAddress::AnyIPv4, 4458, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
	emit statusChanged();
	return ok;
}

void RemoteServer::stop()
{
	server_.close();
	discoverySocket_.close();
	emit statusChanged();
}

void RemoteServer::setToken(const QString &token)
{
	const QString trimmed = token.trimmed();
	if (!trimmed.isEmpty())
		token_ = trimmed;
	emit statusChanged();
}

void RemoteServer::regenerateToken()
{
	token_ = QString::number(QRandomGenerator::global()->bounded(100000, 999999));
	emit statusChanged();
}

void RemoteServer::acceptClient()
{
	while (QTcpSocket *socket = server_.nextPendingConnection()) {
		socket->setSocketOption(QAbstractSocket::LowDelayOption, 1);
		socket->setSocketOption(QAbstractSocket::KeepAliveOption, 1);
		connect(socket, &QTcpSocket::readyRead, this, &RemoteServer::readClient);
		connect(socket, &QTcpSocket::disconnected, this, &RemoteServer::removeClient);
		socket->write(makeStatus());
	}
	emit statusChanged();
}

void RemoteServer::readClient()
{
	auto *socket = qobject_cast<QTcpSocket *>(sender());
	if (!socket)
		return;

	while (socket->canReadLine()) {
		const QByteArray line = socket->readLine().trimmed();
		QJsonParseError error;
		const QJsonDocument document = QJsonDocument::fromJson(line, &error);
		if (error.error != QJsonParseError::NoError || !document.isObject()) {
			socket->write(makeStatus(false, QStringLiteral("bad-json")));
			continue;
		}
		handleCommand(socket, document.object());
	}
}

void RemoteServer::removeClient()
{
	if (auto *socket = qobject_cast<QTcpSocket *>(sender()))
		socket->deleteLater();
	emit statusChanged();
}

void RemoteServer::readDiscoveryDatagram()
{
	while (discoverySocket_.hasPendingDatagrams()) {
		QByteArray datagram;
		datagram.resize(int(discoverySocket_.pendingDatagramSize()));
		QHostAddress sender;
		quint16 senderPort = 0;
		discoverySocket_.readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);

		QJsonParseError error;
		const QJsonDocument document = QJsonDocument::fromJson(datagram.trimmed(), &error);
		if (error.error != QJsonParseError::NoError || !document.isObject())
			continue;

		const QJsonObject object = document.object();
		const QString type = object["type"].toString();
		if (type != QStringLiteral("o-prompter-discover") && type != QStringLiteral("obs-telep-discover"))
			continue;

		const QByteArray response = makeDiscoveryResponse();
		discoverySocket_.writeDatagram(response, sender, senderPort);
	}
}

void RemoteServer::handleCommand(QTcpSocket *socket, const QJsonObject &message)
{
	if (message["token"].toString() != token_) {
		socket->write(makeStatus(false, QStringLiteral("unauthorized")));
		return;
	}

	const QString command = message["command"].toString();
	if (command == "playPause") {
		if (!state_->isPlaying())
			state_->setAudioPauseOverride(true);
		state_->playPause();
	} else if (command == "play") {
		state_->setAudioPauseOverride(true);
		state_->play();
	} else if (command == "pause")
		state_->pause();
	else if (command == "stop")
		state_->stop();
	else if (command == "restart")
		state_->restart();
	else if (command == "top")
		state_->jumpToTop();
	else if (command == "speedDelta")
		state_->changeSpeed(message["value"].toDouble());
	else if (command == "fontSizeDelta")
		state_->changeFontSize(message["value"].toInt());
	else if (command == "jog")
		state_->setJogMultiplier(message["value"].toDouble());
	else if (command == "nextMarker")
		state_->jumpToNextMarker();
	else if (command == "previousMarker")
		state_->jumpToPreviousMarker();
	else if (command == "loadUrl")
		loadScriptFromUrl(message["url"].toString(message["value"].toString()));
	else if (command != "status") {
		socket->write(makeStatus(false, QStringLiteral("unknown-command")));
		return;
	}

	socket->write(makeStatus());
}

void RemoteServer::loadScriptFromUrl(const QString &urlText)
{
	const QUrl url = TelePTextUrlFromInput(urlText);
	if (!url.isValid() || url.scheme().isEmpty())
		return;

	auto *reply = network_.get(TelePTextRequest(url));
	connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
		const QByteArray body = reply->readAll();
		const bool ok = reply->error() == QNetworkReply::NoError;
		reply->deleteLater();

		const auto applyText = [this, url](const QByteArray &data) {
			const QString text = QString::fromUtf8(data);
			if (text.trimmed().isEmpty())
				return;
			state_->setTitle(TelePTitleFromUrl(url));
			state_->setScript(text);
		};

		if (ok) {
			applyText(body);
			return;
		}

		auto *process = new QProcess(this);
		connect(process, &QProcess::finished, this, [process, applyText](int exitCode, QProcess::ExitStatus exitStatus) {
			const QByteArray output = process->readAllStandardOutput();
			process->deleteLater();
			if (exitStatus == QProcess::NormalExit && exitCode == 0)
				applyText(output);
		});
		process->start(QStringLiteral("curl.exe"), TelePCurlTextArguments(url));
		if (!process->waitForStarted(3000))
			process->deleteLater();
	});
}

QByteArray RemoteServer::makeDiscoveryResponse() const
{
	QJsonObject object;
	object["type"] = "o-prompter";
	object["name"] = "O-Prompter";
	object["title"] = state_->title();
	object["port"] = int(port());
	object["discoveryPort"] = 4458;
	object["version"] = 1;
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QByteArray RemoteServer::makeStatus(bool ok, const QString &error) const
{
	QJsonObject object;
	object["ok"] = ok;
	if (!ok)
		object["error"] = error;
	object["title"] = state_->title();
	object["playing"] = state_->isPlaying();
	object["speed"] = state_->speed();
	object["fontSize"] = state_->style().fontSize;
	object["progress"] = state_->progress();
	object["positionPx"] = state_->position();
	object["port"] = int(port());
	object["listening"] = isListening();
	return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}
