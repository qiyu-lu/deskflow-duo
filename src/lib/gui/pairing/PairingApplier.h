/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "PairingService.h"
#include "gui/config/ServerConfig.h"
#include "gui/sync/SyncManager.h"

#include <QObject>

namespace deskflow::gui::pairing {

/**
 * @brief Turns a confirmed pairing into configuration: input sharing layout and role,
 * trusted TLS certificate, Syncthing device and shared folder.
 */
class PairingApplier : public QObject
{
  Q_OBJECT

public:
  PairingApplier(ServerConfig &serverConfig, sync::SyncManager &syncManager, QObject *parent = nullptr);

  void apply(const PairingInfo &local, const PairingInfo &remote, const QString &remoteAddress);

Q_SIGNALS:
  void stepStarted(const QString &title);
  void stepFinished(const QString &title, bool ok, const QString &detail);
  void finished(bool ok, const QString &summary);

private:
  void applyInputSharing();
  void applyTrust();
  void applySyncPeer();
  void applySyncFolder();
  void done();

  ServerConfig &m_serverConfig;
  sync::SyncManager &m_sync;
  PairingInfo m_local;
  PairingInfo m_remote;
  QString m_remoteAddress;
  bool m_ok = true;
  QStringList m_warnings;
};

} // namespace deskflow::gui::pairing
