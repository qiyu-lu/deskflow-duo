/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "PairingService.h"

#include "common/Constants.h"

#include <QCryptographicHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>

namespace deskflow::gui::pairing {

namespace {
const int kConnectTimeoutMs = 15000;
const int kExchangeTimeoutMs = 180000; // the user walks to the other computer and back
const int kMaxMessageSize = 64 * 1024;
const auto kTypeHello = QStringLiteral("hello");
const auto kTypeResult = QStringLiteral("result");
const auto kTypeBye = QStringLiteral("bye");

// "::ffff:192.168.1.5" is how a dual-stack listener reports an IPv4 peer, show it as IPv4
QString plainAddress(const QHostAddress &address)
{
  if (address.isNull()) {
    return {};
  }
  bool isV4 = false;
  const auto v4 = address.toIPv4Address(&isV4);
  return isV4 ? QHostAddress(v4).toString() : address.toString();
}
} // namespace

QJsonObject PairingInfo::toJson() const
{
  return {
      {QStringLiteral("computerName"), computerName},
      {QStringLiteral("syncDeviceId"), syncDeviceId},
      {QStringLiteral("tlsFingerprint"), tlsFingerprint},
      {QStringLiteral("isServer"), isServer},
      {QStringLiteral("clientOnLeft"), clientOnLeft},
      {QStringLiteral("syncFolderPath"), syncFolderPath},
  };
}

PairingInfo PairingInfo::fromJson(const QJsonObject &object)
{
  PairingInfo info;
  info.computerName = object.value(QStringLiteral("computerName")).toString();
  info.syncDeviceId = object.value(QStringLiteral("syncDeviceId")).toString();
  info.tlsFingerprint = object.value(QStringLiteral("tlsFingerprint")).toString();
  info.isServer = object.value(QStringLiteral("isServer")).toBool();
  info.clientOnLeft = object.value(QStringLiteral("clientOnLeft")).toBool();
  info.syncFolderPath = object.value(QStringLiteral("syncFolderPath")).toString();
  return info;
}

PairingService::PairingService(QObject *parent) : QObject(parent)
{
  m_timeout.setSingleShot(true);
  connect(&m_timeout, &QTimer::timeout, this, [this] {
    if (m_phase == Phase::Connecting) {
      fail(tr("Could not reach the other computer. Check the address and that %1 is open there.").arg(kAppName));
    } else if (m_phase == Phase::Listening || m_phase == Phase::Exchanged) {
      fail(tr("Pairing timed out."));
    }
  });
}

PairingService::~PairingService()
{
  abort();
}

void PairingService::setLocalInfo(const PairingInfo &info)
{
  m_local = info;
}

void PairingService::setPhase(Phase phase)
{
  if (m_phase == phase) {
    return;
  }
  m_phase = phase;
  Q_EMIT phaseChanged(phase);
}

bool PairingService::listen(quint16 port)
{
  abort();
  m_server = new QTcpServer(this);
  if (!m_server->listen(QHostAddress::Any, port)) {
    const auto error = m_server->errorString();
    m_server->deleteLater();
    m_server = nullptr;
    fail(tr("Could not listen on port %1: %2").arg(port).arg(error));
    return false;
  }
  connect(m_server, &QTcpServer::newConnection, this, [this] {
    while (auto *socket = m_server->nextPendingConnection()) {
      if (m_socket) {
        // one pairing at a time
        socket->disconnectFromHost();
        socket->deleteLater();
        continue;
      }
      adoptSocket(socket);
    }
  });
  setPhase(Phase::Listening);
  return true;
}

quint16 PairingService::listeningPort() const
{
  return m_server ? m_server->serverPort() : 0;
}

void PairingService::connectToServer(const QString &address, quint16 port)
{
  abort();
  auto *socket = new QTcpSocket(this);
  adoptSocket(socket);
  // what the user typed is what the client will keep connecting to later on
  m_remoteAddress = address.trimmed();
  setPhase(Phase::Connecting);
  m_timeout.start(kConnectTimeoutMs);
  connect(socket, &QTcpSocket::connected, this, [this] {
    m_timeout.start(kExchangeTimeoutMs);
    auto hello = m_local.toJson();
    hello.insert(QStringLiteral("type"), kTypeHello);
    hello.insert(QStringLiteral("version"), kPairingProtocolVersion);
    sendMessage(hello);
    m_helloSent = true;
  });
  socket->connectToHost(address.trimmed(), port);
}

void PairingService::adoptSocket(QTcpSocket *socket)
{
  m_socket = socket;
  m_buffer.clear();
  m_helloSent = false;
  m_helloReceived = false;
  m_remoteAddress = plainAddress(socket->peerAddress());
  connect(socket, &QTcpSocket::readyRead, this, &PairingService::readMessages);
  connect(socket, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
    if (m_phase == Phase::Connecting || m_phase == Phase::Listening || m_phase == Phase::Exchanged) {
      fail(tr("The connection to the other computer failed: %1").arg(m_socket ? m_socket->errorString() : QString()));
    }
  });
  connect(socket, &QTcpSocket::disconnected, this, [this] {
    if (m_phase == Phase::Exchanged || m_phase == Phase::Connecting) {
      fail(tr("The other computer closed the connection."));
    } else if (m_phase == Phase::Listening && m_socket) {
      // the peer went away before saying hello, keep listening for another attempt
      m_socket->deleteLater();
      m_socket = nullptr;
    }
  });
  if (m_phase == Phase::Listening) {
    m_timeout.start(kExchangeTimeoutMs);
  }
}

void PairingService::sendMessage(const QJsonObject &message)
{
  if (!m_socket || message.isEmpty()) {
    return;
  }
  m_socket->write(QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n');
  m_socket->flush();
}

void PairingService::readMessages()
{
  if (!m_socket) {
    return;
  }
  m_buffer.append(m_socket->readAll());
  if (m_buffer.size() > kMaxMessageSize) {
    fail(tr("The other computer sent an invalid pairing message."));
    return;
  }

  int newline = -1;
  while ((newline = static_cast<int>(m_buffer.indexOf('\n'))) >= 0) {
    const auto line = m_buffer.left(newline);
    m_buffer.remove(0, newline + 1);
    const auto document = QJsonDocument::fromJson(line);
    if (!document.isObject()) {
      fail(tr("The other computer sent an invalid pairing message."));
      return;
    }
    handleMessage(document.object());
    if (!m_socket) {
      return;
    }
  }
}

void PairingService::handleMessage(const QJsonObject &message)
{
  const auto type = message.value(QStringLiteral("type")).toString();

  if (type == kTypeHello) {
    if (message.value(QStringLiteral("version")).toInt() != kPairingProtocolVersion) {
      sendMessage({{QStringLiteral("type"), kTypeBye}, {QStringLiteral("reason"), QStringLiteral("version")}});
      fail(tr("The other computer runs a different version of %1. Install the same version on both.").arg(kAppName));
      return;
    }
    m_remote = PairingInfo::fromJson(message);
    m_helloReceived = true;
    if (m_remote.computerName.isEmpty()) {
      fail(tr("The other computer did not send its name."));
      return;
    }
    if (m_remote.isServer == m_local.isServer) {
      sendMessage({{QStringLiteral("type"), kTypeBye}, {QStringLiteral("reason"), QStringLiteral("role")}});
      fail(
          m_local.isServer
              ? tr("Both computers are set as the one with the keyboard. Choose \"controlled\" on the other computer.")
              : tr("Both computers are set as controlled. Choose \"keyboard and mouse are here\" on the other computer.")
      );
      return;
    }
    if (m_remote.computerName.compare(m_local.computerName, Qt::CaseInsensitive) == 0) {
      sendMessage({{QStringLiteral("type"), kTypeBye}, {QStringLiteral("reason"), QStringLiteral("name")}});
      fail(tr("Both computers are called \"%1\". Give one of them a different name first.").arg(m_local.computerName));
      return;
    }

    if (!m_helloSent) {
      auto hello = m_local.toJson();
      hello.insert(QStringLiteral("type"), kTypeHello);
      hello.insert(QStringLiteral("version"), kPairingProtocolVersion);
      sendMessage(hello);
      m_helloSent = true;
    }

    const auto &server = m_local.isServer ? m_local : m_remote;
    const auto &client = m_local.isServer ? m_remote : m_local;
    m_code = makeConfirmationCode(server, client);
    m_timeout.start(kExchangeTimeoutMs);
    setPhase(Phase::Exchanged);
    Q_EMIT exchanged(m_remote, m_code);
    return;
  }

  if (type == kTypeResult) {
    const bool accepted = message.value(QStringLiteral("accepted")).toBool();
    m_timeout.stop();
    setPhase(accepted ? Phase::Accepted : Phase::Rejected);
    closeSocket();
    Q_EMIT finished(accepted);
    return;
  }

  if (type == kTypeBye) {
    const auto reason = message.value(QStringLiteral("reason")).toString();
    if (reason == QStringLiteral("version")) {
      fail(tr("The other computer runs a different version of %1. Install the same version on both.").arg(kAppName));
    } else if (reason == QStringLiteral("role")) {
      fail(tr("Both computers chose the same role. Change it on one of them."));
    } else if (reason == QStringLiteral("name")) {
      fail(tr("Both computers have the same name. Give one of them a different name first."));
    } else {
      fail(tr("The other computer cancelled the pairing."));
    }
  }
}

void PairingService::confirm(bool accepted)
{
  if (m_phase != Phase::Exchanged) {
    return;
  }
  sendMessage({{QStringLiteral("type"), kTypeResult}, {QStringLiteral("accepted"), accepted}});
  m_timeout.stop();
  setPhase(accepted ? Phase::Accepted : Phase::Rejected);
  closeSocket();
  Q_EMIT finished(accepted);
}

void PairingService::abort()
{
  m_timeout.stop();
  if (m_socket && (m_phase == Phase::Exchanged || m_phase == Phase::Connecting)) {
    sendMessage({{QStringLiteral("type"), kTypeBye}, {QStringLiteral("reason"), QStringLiteral("cancel")}});
  }
  closeSocket();
  if (m_server) {
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
  }
  m_buffer.clear();
  m_helloSent = false;
  m_helloReceived = false;
  setPhase(Phase::Idle);
}

void PairingService::fail(const QString &error)
{
  m_timeout.stop();
  closeSocket();
  if (m_server) {
    m_server->close();
    m_server->deleteLater();
    m_server = nullptr;
  }
  setPhase(Phase::Failed);
  Q_EMIT failed(error);
}

void PairingService::closeSocket()
{
  if (!m_socket) {
    return;
  }
  auto *socket = m_socket;
  m_socket = nullptr;
  disconnect(socket, nullptr, this, nullptr);
  socket->disconnectFromHost();
  socket->deleteLater();
}

QString PairingService::makeConfirmationCode(const PairingInfo &server, const PairingInfo &client)
{
  QCryptographicHash hash(QCryptographicHash::Sha256);
  hash.addData(QByteArrayLiteral("deskflow-pairing-v1\n"));
  for (const auto &part :
       {server.computerName, server.syncDeviceId, server.tlsFingerprint, client.computerName, client.syncDeviceId,
        client.tlsFingerprint}) {
    hash.addData(part.toUtf8());
    hash.addData(QByteArrayLiteral("\n"));
  }
  const auto digest = hash.result();
  quint32 value = 0;
  for (int i = 0; i < 4; i++) {
    value = (value << 8) | static_cast<quint8>(digest.at(i));
  }
  return QStringLiteral("%1").arg(value % 1000000, 6, 10, QLatin1Char('0'));
}

} // namespace deskflow::gui::pairing
