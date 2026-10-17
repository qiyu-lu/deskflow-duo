/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SyncManager.h"

#include "common/Constants.h"
#include "common/Settings.h"

#include <QDir>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QTimer>

namespace deskflow::gui::sync {

SyncManager::SyncManager(QObject *parent)
    : QObject(parent),
      m_process{new SyncthingProcess(this)},
      m_client{new SyncthingClient(this)},
      m_monitor{new SyncthingMonitor(m_client, this)}
{
  connect(m_process, &SyncthingProcess::stateChanged, this, &SyncManager::onProcessStateChanged);
  connect(m_process, &SyncthingProcess::apiEndpointChanged, this, [this](const QUrl &url, const QByteArray &key) {
    m_client->setEndpoint(url, key);
    m_monitor->setActive(true);
  });
  connect(m_process, &SyncthingProcess::errorOccurred, this, &SyncManager::errorOccurred);
  // keep the cached folder path current before anyone else looks at the new summary
  connect(m_monitor, &SyncthingMonitor::summaryChanged, this, &SyncManager::rememberFolderPath);
  connect(m_monitor, &SyncthingMonitor::summaryChanged, this, &SyncManager::summaryChanged);
}

SyncManager::~SyncManager()
{
  m_monitor->setActive(false);
  m_client->abortAll();
}

bool SyncManager::isEnabled() const
{
  return Settings::value(Settings::Sync::Enabled).toBool();
}

void SyncManager::start()
{
  if (!isEnabled()) {
    return;
  }
  m_process->start();
}

void SyncManager::stop()
{
  m_monitor->setActive(false);
  if (m_process->isRunning()) {
    // ask for a clean shutdown through the API, the process class falls back to terminating it
    m_client->shutdown({});
  }
  m_client->clearEndpoint();
  m_process->stop();
}

void SyncManager::shutdown(int timeoutMs)
{
  m_monitor->setActive(false);
  if (!m_process->isRunning()) {
    m_client->clearEndpoint();
    m_process->stop();
    return;
  }

  QEventLoop loop;
  QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
  connect(m_process, &SyncthingProcess::stateChanged, &loop, [&loop](SyncthingProcess::State state) {
    if (state == SyncthingProcess::State::Stopped || state == SyncthingProcess::State::Failed) {
      loop.quit();
    }
  });
  // a clean shutdown through the API works everywhere; the signal is the fallback on unix
  m_client->shutdown({});
  m_process->stop();
  loop.exec();
  m_client->clearEndpoint();
}

QString SyncManager::folderPath() const
{
  const auto live = livePathOf(folderId());
  if (!live.isEmpty()) {
    return live;
  }
  return SyncthingMonitor::expandPath(Settings::value(Settings::Sync::FolderPath).toString());
}

QString SyncManager::livePathOf(const QString &folderId) const
{
  if (!m_monitor->summary().apiReachable) {
    return {};
  }
  for (const auto &folder : m_monitor->summary().folders) {
    if (folder.id == folderId) {
      return folder.path;
    }
  }
  return {};
}

void SyncManager::rememberFolderPath(const SyncSummary &summary)
{
  if (!summary.apiReachable) {
    return;
  }
  const auto id = folderId();
  for (const auto &folder : summary.folders) {
    if (folder.id == id && !folder.path.isEmpty()) {
      // setValue ignores unchanged values, so this only writes when the folder really moved
      Settings::setValue(Settings::Sync::FolderPath, folder.path);
      return;
    }
  }
}

QString SyncManager::folderId() const
{
  return Settings::value(Settings::Sync::FolderId).toString();
}

void SyncManager::setPaused(bool paused, Callback callback)
{
  m_client->pauseAll(paused, [this, callback](const SyncthingClient::Reply &reply) {
    m_monitor->refreshNow();
    if (callback) {
      callback(reply);
    }
  });
}

void SyncManager::refresh()
{
  m_monitor->refreshNow();
}

void SyncManager::setPeer(const QString &deviceId, const QString &name, const QStringList &addresses, Callback callback)
{
  // the addresses the peer told us about come first, "dynamic" keeps discovery as the fallback
  QStringList all;
  for (const auto &address : addresses) {
    if (!address.isEmpty() && address != QStringLiteral("dynamic") && !all.contains(address)) {
      all.append(address);
    }
  }
  all.append(QStringLiteral("dynamic"));

  QJsonObject device;
  device.insert(QStringLiteral("deviceID"), deviceId);
  device.insert(QStringLiteral("name"), name);
  device.insert(QStringLiteral("addresses"), QJsonArray::fromStringList(all));
  device.insert(QStringLiteral("autoAcceptFolders"), true);
  device.insert(QStringLiteral("paused"), false);
  m_client->setDevice(device, [this, callback](const SyncthingClient::Reply &reply) {
    m_monitor->refreshNow();
    if (callback) {
      callback(reply);
    }
  });
}

void SyncManager::ensureSharedFolder(const QString &peerDeviceId, const QString &path, Callback callback)
{
  const auto requestedPath = QDir::cleanPath(path);
  const auto id = folderId();
  m_client->getFolder(id, [this, id, requestedPath, peerDeviceId, callback](const SyncthingClient::Reply &existing) {
    // only a 404 means "no such folder yet"; on any other failure we cannot tell, so do not overwrite anything
    if (!existing.ok && existing.status != 404) {
      if (callback) {
        callback(existing);
      }
      return;
    }

    QJsonObject folder;
    QJsonArray devices;
    // Syncthing never moves an existing folder (its web interface keeps the path read-only, and pointing a folder
    // at another directory can look like mass deletion), so an existing folder keeps its own path.
    QString folderPath = requestedPath;
    if (existing.ok) {
      folder = existing.object();
      devices = folder.value(QStringLiteral("devices")).toArray();
      const auto currentPath = folder.value(QStringLiteral("path")).toString();
      if (!currentPath.isEmpty()) {
        folderPath = currentPath;
      }
    }

    const auto localPath = SyncthingMonitor::expandPath(folderPath);
    if (!QDir().mkpath(localPath)) {
      SyncthingClient::Reply reply;
      reply.error = tr("Could not create the folder %1").arg(localPath);
      if (callback) {
        callback(reply);
      }
      return;
    }

    bool shared = false;
    for (const auto &value : std::as_const(devices)) {
      if (value.toObject().value(QStringLiteral("deviceID")).toString() == peerDeviceId) {
        shared = true;
      }
    }
    if (!shared) {
      devices.append(QJsonObject{{QStringLiteral("deviceID"), peerDeviceId}});
    }

    folder.insert(QStringLiteral("id"), id);
    folder.insert(QStringLiteral("label"), QString(kSyncFolderLabel));
    folder.insert(QStringLiteral("path"), folderPath);
    folder.insert(QStringLiteral("type"), QStringLiteral("sendreceive"));
    folder.insert(QStringLiteral("devices"), devices);
    folder.insert(QStringLiteral("paused"), false);
    folder.insert(QStringLiteral("fsWatcherEnabled"), true);

    m_client->setFolder(folder, [this, localPath, callback](const SyncthingClient::Reply &reply) {
      if (reply.ok) {
        Settings::setValue(Settings::Sync::FolderPath, localPath);
      }
      m_monitor->refreshNow();
      if (callback) {
        callback(reply);
      }
    });
  });
}

void SyncManager::setDefaultFolderParent(const QString &path, Callback callback)
{
  QJsonObject changes;
  changes.insert(QStringLiteral("path"), QDir::cleanPath(path));
  m_client->setDefaultFolder(changes, std::move(callback));
}

void SyncManager::onProcessStateChanged(SyncthingProcess::State state)
{
  if (state != SyncthingProcess::State::Running) {
    m_monitor->setActive(false);
    m_client->clearEndpoint();
  }
  Q_EMIT processStateChanged(state);
}

} // namespace deskflow::gui::sync
