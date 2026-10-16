/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "SyncSummary.h"
#include "SyncthingClient.h"
#include "SyncthingMonitor.h"
#include "SyncthingProcess.h"

#include <QObject>

namespace deskflow::gui::sync {

/**
 * @brief Facade over the managed Syncthing instance: process, REST client and status monitor.
 *
 * The GUI only talks to this class. Besides starting and stopping the instance it offers the
 * few configuration changes the guided pairing needs; everything else is left to the Syncthing
 * web interface, which the user can open from the GUI.
 */
class SyncManager : public QObject
{
  Q_OBJECT

public:
  using Callback = SyncthingClient::Callback;

  explicit SyncManager(QObject *parent = nullptr);
  ~SyncManager() override;

  bool isEnabled() const;
  void start();
  void stop();

  /**
   * @brief Ask the instance to shut down and wait for it, used when the application quits.
   */
  void shutdown(int timeoutMs = 5000);

  SyncthingProcess *process() const
  {
    return m_process;
  }
  SyncthingClient *client() const
  {
    return m_client;
  }
  const SyncSummary &summary() const
  {
    return m_monitor->summary();
  }
  SyncthingProcess::State processState() const
  {
    return m_process->state();
  }
  bool isApiReachable() const
  {
    return m_monitor->summary().apiReachable;
  }
  QString myDeviceId() const
  {
    return m_monitor->summary().myId;
  }
  QUrl webUiUrl() const
  {
    return m_process->apiUrl();
  }
  QString folderPath() const;
  QString folderId() const;

  void setPaused(bool paused, Callback callback = {});
  void refresh();

  /**
   * @brief Add (or update) the paired computer as a Syncthing device.
   */
  void setPeer(const QString &deviceId, const QString &name, Callback callback);

  /**
   * @brief Create or update the shared folder and share it with the given device.
   */
  void ensureSharedFolder(const QString &peerDeviceId, const QString &path, Callback callback);

  /**
   * @brief Folders shared from the peer later on (through the web UI) land below this path.
   */
  void setDefaultFolderParent(const QString &path, Callback callback);

Q_SIGNALS:
  void summaryChanged(const deskflow::gui::sync::SyncSummary &summary);
  void processStateChanged(deskflow::gui::sync::SyncthingProcess::State state);
  void errorOccurred(const QString &message);

private:
  void onProcessStateChanged(SyncthingProcess::State state);

  SyncthingProcess *m_process;
  SyncthingClient *m_client;
  SyncthingMonitor *m_monitor;
};

} // namespace deskflow::gui::sync
