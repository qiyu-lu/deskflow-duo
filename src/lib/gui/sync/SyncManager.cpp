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
  return QDir::cleanPath(Settings::value(Settings::Sync::FolderPath).toString());
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

void SyncManager::setPeer(const QString &deviceId, const QString &name, Callback callback)
{
  QJsonObject device;
  device.insert(QStringLiteral("deviceID"), deviceId);
  device.insert(QStringLiteral("name"), name);
  device.insert(QStringLiteral("addresses"), QJsonArray{QStringLiteral("dynamic")});
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
  const auto cleanPath = QDir::cleanPath(path);
  if (!QDir().mkpath(cleanPath)) {
    SyncthingClient::Reply reply;
    reply.error = tr("Could not create the folder %1").arg(cleanPath);
    if (callback) {
      callback(reply);
    }
    return;
  }

  const auto id = folderId();
  m_client->getFolder(id, [this, id, cleanPath, peerDeviceId, callback](const SyncthingClient::Reply &existing) {
    QJsonObject folder;
    QJsonArray devices;
    if (existing.ok) {
      folder = existing.object();
      devices = folder.value(QStringLiteral("devices")).toArray();
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
    folder.insert(QStringLiteral("path"), cleanPath);
    folder.insert(QStringLiteral("type"), QStringLiteral("sendreceive"));
    folder.insert(QStringLiteral("devices"), devices);
    folder.insert(QStringLiteral("paused"), false);
    folder.insert(QStringLiteral("fsWatcherEnabled"), true);

    m_client->setFolder(folder, [this, cleanPath, callback](const SyncthingClient::Reply &reply) {
      if (reply.ok) {
        Settings::setValue(Settings::Sync::FolderPath, cleanPath);
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
