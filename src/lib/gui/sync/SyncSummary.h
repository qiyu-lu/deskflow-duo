/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QList>
#include <QString>

namespace deskflow::gui::sync {

/** @brief Condensed view of the managed Syncthing instance, refreshed by SyncthingMonitor. */
struct SyncSummary
{
  enum class Overall
  {
    Unavailable, //!< the REST API could not be reached
    NoPeer,      //!< no remote device configured yet
    PeerOffline, //!< no configured device is connected
    Paused,      //!< every device or folder is paused
    Scanning,
    Syncing,
    OutOfSync, //!< a folder reported errors or cannot complete
    Idle       //!< everything is up to date
  };

  struct Peer
  {
    QString id;
    QString name;
    QString address;
    bool connected = false;
    bool paused = false;
    double completion = 100.0; //!< how much of our data the peer has, in percent
  };

  struct Folder
  {
    QString id;
    QString label;
    QString path;
    QString state; //!< raw syncthing state: idle, scanning, syncing, error, ...
    QString error;
    bool paused = false;
    bool shared = false;
    qint64 globalBytes = 0;
    qint64 inSyncBytes = 0;
    qint64 needItems = 0;
    int errorCount = 0;
  };

  bool apiReachable = false;
  QString myId;
  QString myName;
  QString lastError;
  QList<Peer> peers;
  QList<Folder> folders;
  Overall overall = Overall::Unavailable;
  int progressPercent = 100;

  bool anyPeerConnected() const
  {
    for (const auto &peer : peers) {
      if (peer.connected) {
        return true;
      }
    }
    return false;
  }
};

} // namespace deskflow::gui::sync
