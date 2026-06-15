#pragma once

#include "teleprompter-state.hpp"

#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QUdpSocket>

class RemoteServer : public QObject {
	Q_OBJECT

public:
	explicit RemoteServer(TeleprompterState *state, QObject *parent = nullptr);

	bool start(quint16 port);
	void stop();
	bool isListening() const { return server_.isListening(); }
	quint16 port() const { return server_.serverPort(); }
	QString token() const { return token_; }
	void setToken(const QString &token);
	void regenerateToken();

signals:
	void statusChanged();

private slots:
	void acceptClient();
	void readClient();
	void removeClient();
	void readDiscoveryDatagram();

private:
	QByteArray makeStatus(bool ok = true, const QString &error = QString()) const;
	QByteArray makeDiscoveryResponse() const;
	void handleCommand(QTcpSocket *socket, const QJsonObject &message);
	void loadScriptFromUrl(const QString &urlText);

	TeleprompterState *state_ = nullptr;
	QTcpServer server_;
	QNetworkAccessManager network_;
	QUdpSocket discoverySocket_;
	QString token_;
};
