/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

class QTcpServer;
class QTcpSocket;

namespace deskflow::gui::pairing {

/**
 * @brief What one computer tells the other during pairing.
 */
struct PairingInfo
{
  QString computerName;   //!< Deskflow screen name
  QString syncDeviceId;   //!< Syncthing device ID, empty when file sync is unavailable
  QString tlsFingerprint; //!< SHA-256 of the Deskflow TLS certificate as hex, empty when TLS is off
  bool isServer = false;  //!< keyboard and mouse are attached to this computer
  bool clientOnLeft = false; //!< server only: the client sits to the left of the server
  QString syncFolderPath; //!< informational, the shared folder on that computer

  QJsonObject toJson() const;
  static PairingInfo fromJson(const QJsonObject &object);
};

/**
 * @brief Exchanges PairingInfo between two computers on the LAN and lets the user confirm
 * the exchange with a short numeric code, the same way Bluetooth numeric comparison works.
 *
 * The computer with the keyboard (the server) listens; the other one connects. Messages are
 * newline-delimited JSON objects with a "type" field: hello, result, bye.
 */
class PairingService : public QObject
{
  Q_OBJECT

public:
  enum class Phase
  {
    Idle,
    Listening,
    Connecting,
    Exchanged, //!< both sides know each other, waiting for the server user to confirm
    Accepted,
    Rejected,
    Failed
  };
  Q_ENUM(Phase)

  explicit PairingService(QObject *parent = nullptr);
  ~PairingService() override;

  void setLocalInfo(const PairingInfo &info);
  const PairingInfo &localInfo() const
  {
    return m_local;
  }
  const PairingInfo &remoteInfo() const
  {
    return m_remote;
  }
  Phase phase() const
  {
    return m_phase;
  }
  QString confirmationCode() const
  {
    return m_code;
  }
  QString remoteAddress() const
  {
    return m_remoteAddress;
  }
  /** @brief Server side: the port actually bound by listen(), useful when 0 was requested. */
  quint16 listeningPort() const;

  /** @brief Server side: wait for the other computer. */
  bool listen(quint16 port);
  /** @brief Client side: connect to the server computer. */
  void connectToServer(const QString &address, quint16 port);
  /** @brief Server side: answer the confirmation prompt. */
  void confirm(bool accepted);
  void abort();

  static QString makeConfirmationCode(const PairingInfo &server, const PairingInfo &client);

Q_SIGNALS:
  void phaseChanged(deskflow::gui::pairing::PairingService::Phase phase);
  void exchanged(const deskflow::gui::pairing::PairingInfo &remote, const QString &code);
  void finished(bool accepted);
  void failed(const QString &error);

private:
  void setPhase(Phase phase);
  void adoptSocket(QTcpSocket *socket);
  void sendMessage(const QJsonObject &message);
  void readMessages();
  void handleMessage(const QJsonObject &message);
  void fail(const QString &error);
  void closeSocket();

  PairingInfo m_local;
  PairingInfo m_remote;
  Phase m_phase = Phase::Idle;
  QString m_code;
  QString m_remoteAddress;
  QTcpServer *m_server = nullptr;
  QTcpSocket *m_socket = nullptr;
  QTimer m_timeout;
  QByteArray m_buffer;
  bool m_helloSent = false;
  bool m_helloReceived = false;
};

} // namespace deskflow::gui::pairing
