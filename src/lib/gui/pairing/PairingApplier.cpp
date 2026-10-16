/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "PairingApplier.h"

#include "common/Settings.h"
#include "gui/TlsUtility.h"
#include "net/FingerprintDatabase.h"

#include <QDir>

namespace deskflow::gui::pairing {

PairingApplier::PairingApplier(ServerConfig &serverConfig, sync::SyncManager &syncManager, QObject *parent)
    : QObject(parent),
      m_serverConfig(serverConfig),
      m_sync(syncManager)
{
}

void PairingApplier::apply(const PairingInfo &local, const PairingInfo &remote, const QString &remoteAddress)
{
  m_local = local;
  m_remote = remote;
  m_remoteAddress = remoteAddress;
  m_ok = true;
  m_warnings.clear();
  applyInputSharing();
}

void PairingApplier::applyInputSharing()
{
  const auto title = tr("Keyboard and mouse sharing");
  Q_EMIT stepStarted(title);

  Settings::setValue(Settings::Core::CoreMode, m_local.isServer ? Settings::CoreMode::Server : Settings::CoreMode::Client);
  if (m_local.isServer) {
    m_serverConfig.setupTwoComputerLayout(m_remote.computerName, m_local.clientOnLeft);
    m_serverConfig.commit();
    Settings::setValue(Settings::Server::ExternalConfig, false);
    // the wizard already explained what happens next, skip the generic first-start hint
    Settings::setValue(Settings::Gui::ShownServerFirstStartMessage, true);
  } else {
    Settings::setValue(Settings::Client::RemoteHost, m_remoteAddress);
  }

  Settings::setValue(Settings::Pairing::Completed, true);
  Settings::setValue(Settings::Pairing::PeerName, m_remote.computerName);
  Settings::setValue(Settings::Pairing::PeerAddress, m_remoteAddress);
  Settings::setValue(Settings::Pairing::PeerSyncDeviceId, m_remote.syncDeviceId);
  Settings::setValue(
      Settings::Pairing::PeerSide,
      m_local.isServer ? (m_local.clientOnLeft ? QStringLiteral("left") : QStringLiteral("right"))
                       : (m_remote.clientOnLeft ? QStringLiteral("right") : QStringLiteral("left"))
  );
  Settings::save();

  const auto detail = m_local.isServer ? tr("%1 is placed %2 of this computer.")
                                            .arg(m_remote.computerName, m_local.clientOnLeft ? tr("left") : tr("right"))
                                      : tr("This computer connects to %1 at %2.").arg(m_remote.computerName, m_remoteAddress);
  Q_EMIT stepFinished(title, true, detail);
  applyTrust();
}

void PairingApplier::applyTrust()
{
  const auto title = tr("Trusting the other computer");
  Q_EMIT stepStarted(title);

  if (!TlsUtility::isEnabled()) {
    Q_EMIT stepFinished(title, true, tr("TLS is turned off, nothing to trust."));
    applySyncPeer();
    return;
  }
  if (m_remote.tlsFingerprint.isEmpty()) {
    m_warnings.append(tr("The other computer did not send a certificate fingerprint. You will be asked to confirm it on "
                         "the first connection."));
    Q_EMIT stepFinished(title, true, tr("No fingerprint received, it will be confirmed on first connection."));
    applySyncPeer();
    return;
  }

  const Fingerprint fingerprint = {
      QCryptographicHash::Sha256, QByteArray::fromHex(m_remote.tlsFingerprint.toLatin1())
  };
  const auto database = m_local.isServer ? Settings::tlsTrustedClientsDb() : Settings::tlsTrustedServersDb();
  FingerprintDatabase db;
  db.read(database);
  if (!db.isTrusted(fingerprint)) {
    db.addTrusted(fingerprint);
    if (!db.write(database)) {
      m_warnings.append(tr("Could not save the trusted certificate to %1.").arg(database));
      Q_EMIT stepFinished(title, false, tr("Could not write %1").arg(database));
      applySyncPeer();
      return;
    }
  }
  Q_EMIT stepFinished(title, true, tr("Certificate of %1 is trusted.").arg(m_remote.computerName));
  applySyncPeer();
}

void PairingApplier::applySyncPeer()
{
  const auto title = tr("File sync device");
  Q_EMIT stepStarted(title);

  if (!m_sync.isEnabled()) {
    Q_EMIT stepFinished(title, true, tr("File sync is turned off."));
    done();
    return;
  }
  if (m_remote.syncDeviceId.isEmpty() || m_local.syncDeviceId.isEmpty() || !m_sync.isApiReachable()) {
    m_warnings.append(tr("File sync was not ready on both computers, so it was skipped. Pair again later to set it up."));
    Q_EMIT stepFinished(title, true, tr("Skipped, file sync was not ready."));
    done();
    return;
  }

  m_sync.setPeer(m_remote.syncDeviceId, m_remote.computerName, [this, title](const sync::SyncthingClient::Reply &reply) {
    if (!reply.ok) {
      m_warnings.append(tr("Could not add %1 to file sync: %2").arg(m_remote.computerName, reply.error));
      Q_EMIT stepFinished(title, false, reply.error);
      done();
      return;
    }
    Q_EMIT stepFinished(title, true, tr("%1 added.").arg(m_remote.computerName));
    applySyncFolder();
  });
}

void PairingApplier::applySyncFolder()
{
  const auto title = tr("Shared folder");
  Q_EMIT stepStarted(title);

  const auto path = m_sync.folderPath();
  m_sync.ensureSharedFolder(m_remote.syncDeviceId, path, [this, title, path](const sync::SyncthingClient::Reply &reply) {
    if (!reply.ok) {
      m_warnings.append(tr("Could not create the shared folder: %1").arg(reply.error));
      Q_EMIT stepFinished(title, false, reply.error);
      done();
      return;
    }
    Q_EMIT stepFinished(title, true, path);
    // folders shared later on through the web UI land in the home directory
    m_sync.setDefaultFolderParent(QDir::homePath(), [this](const sync::SyncthingClient::Reply &) { done(); });
  });
}

void PairingApplier::done()
{
  QString summary;
  if (m_warnings.isEmpty()) {
    summary = tr("Pairing with %1 is complete.").arg(m_remote.computerName);
  } else {
    summary = tr("Pairing with %1 is complete with remarks:\n\n%2").arg(m_remote.computerName, m_warnings.join("\n"));
  }
  Q_EMIT finished(m_ok, summary);
}

} // namespace deskflow::gui::pairing
