/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SyncthingMonitor.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>

namespace {

// keep only tcp/quic listeners with a concrete LAN host; 0.0.0.0, loopback and link-local entries are useless to a peer
bool isUsableLanAddress(const QString &address)
{
  const QUrl url(address);
  const auto scheme = url.scheme();
  if (!scheme.startsWith(QStringLiteral("tcp")) && !scheme.startsWith(QStringLiteral("quic"))) {
    return false;
  }
  const QHostAddress host(url.host());
  if (host.isNull() || host.isLoopback() || host.isLinkLocal()) {
    return false;
  }
  if (host == QHostAddress::Any || host == QHostAddress::AnyIPv4 || host == QHostAddress::AnyIPv6) {
    return false;
  }
  return url.port() > 0;
}

} // namespace

namespace deskflow::gui::sync {

namespace {
const int kPollIntervalMs = 3000;
const int kUnreachablePollIntervalMs = 2000;

bool isSyncingState(const QString &state)
{
  return state == QStringLiteral("syncing") || state == QStringLiteral("sync-preparing") ||
         state == QStringLiteral("sync-waiting");
}

bool isScanningState(const QString &state)
{
  return state == QStringLiteral("scanning") || state == QStringLiteral("scan-waiting") ||
         state == QStringLiteral("cleaning") || state == QStringLiteral("clean-waiting");
}
} // namespace

SyncthingMonitor::SyncthingMonitor(SyncthingClient *client, QObject *parent) : QObject(parent), m_client{client}
{
  m_timer.setSingleShot(true);
  connect(&m_timer, &QTimer::timeout, this, &SyncthingMonitor::poll);
}

SyncthingMonitor::~SyncthingMonitor()
{
  // invalidate callbacks of any round still in flight
  m_round++;
}

void SyncthingMonitor::setActive(bool active)
{
  if (active) {
    if (!m_timer.isActive() && !m_inFlight) {
      poll();
    }
  } else {
    m_timer.stop();
    m_round++; // drop replies of the current round
    m_inFlight = false;
    m_summary = SyncSummary();
    Q_EMIT summaryChanged(m_summary);
  }
}

void SyncthingMonitor::refreshNow()
{
  if (!m_inFlight) {
    m_timer.stop();
    poll();
  }
}

void SyncthingMonitor::poll()
{
  m_inFlight = true;
  const int round = ++m_round;
  m_next = SyncSummary();
  m_outstanding = 0;

  m_client->systemStatus([this, round](const SyncthingClient::Reply &reply) {
    if (round != m_round) {
      return;
    }
    if (!reply.ok) {
      m_next.apiReachable = false;
      m_next.lastError = reply.error;
      finishRound(round);
      return;
    }
    m_next.apiReachable = true;
    const auto status = reply.object();
    m_next.myId = status.value(QStringLiteral("myID")).toString();
    const auto listeners = status.value(QStringLiteral("connectionServiceStatus")).toObject();
    for (auto it = listeners.constBegin(); it != listeners.constEnd(); ++it) {
      const auto listener = it.value().toObject();
      const auto error = listener.value(QStringLiteral("error")).toString();
      if (!error.isEmpty() && m_next.listenError.isEmpty()) {
        m_next.listenError = QStringLiteral("%1: %2").arg(it.key(), error);
      }
      const auto addresses = listener.value(QStringLiteral("lanAddresses")).toArray();
      for (const auto &value : addresses) {
        const auto address = value.toString();
        if (isUsableLanAddress(address) && !m_next.listenAddresses.contains(address)) {
          m_next.listenAddresses.append(address);
        }
      }
    }
    gatherDetails(round);
  });
}

void SyncthingMonitor::gatherDetails(int round)
{
  m_client->config([this, round](const SyncthingClient::Reply &reply) {
    if (round != m_round) {
      return;
    }
    if (!reply.ok) {
      m_next.lastError = reply.error;
      finishRound(round);
      return;
    }

    const auto config = reply.object();
    const auto devices = config.value(QStringLiteral("devices")).toArray();
    for (const auto &value : devices) {
      const auto device = value.toObject();
      const auto id = device.value(QStringLiteral("deviceID")).toString();
      if (id == m_next.myId) {
        m_next.myName = device.value(QStringLiteral("name")).toString();
        continue;
      }
      SyncSummary::Peer peer;
      peer.id = id;
      peer.name = device.value(QStringLiteral("name")).toString();
      peer.paused = device.value(QStringLiteral("paused")).toBool();
      m_next.peers.append(peer);
    }

    const auto folders = config.value(QStringLiteral("folders")).toArray();
    for (const auto &value : folders) {
      const auto folder = value.toObject();
      SyncSummary::Folder entry;
      entry.id = folder.value(QStringLiteral("id")).toString();
      entry.label = folder.value(QStringLiteral("label")).toString();
      entry.path = folder.value(QStringLiteral("path")).toString();
      entry.paused = folder.value(QStringLiteral("paused")).toBool();
      entry.shared = folder.value(QStringLiteral("devices")).toArray().size() > 1;
      m_next.folders.append(entry);
    }

    // connections, per folder status, per peer completion, errors
    expectReply(round);
    m_client->connections([this, round](const SyncthingClient::Reply &reply) {
      if (round != m_round) {
        return;
      }
      if (reply.ok) {
        const auto connections = reply.object().value(QStringLiteral("connections")).toObject();
        for (auto &peer : m_next.peers) {
          const auto info = connections.value(peer.id).toObject();
          peer.connected = info.value(QStringLiteral("connected")).toBool();
          peer.address = info.value(QStringLiteral("address")).toString();
          if (info.contains(QStringLiteral("paused"))) {
            peer.paused = peer.paused || info.value(QStringLiteral("paused")).toBool();
          }
        }
      }
      replyDone(round);
    });

    for (int i = 0; i < m_next.folders.size(); i++) {
      expectReply(round);
      m_client->dbStatus(m_next.folders[i].id, [this, round, i](const SyncthingClient::Reply &reply) {
        if (round != m_round) {
          return;
        }
        if (reply.ok && i < m_next.folders.size()) {
          const auto status = reply.object();
          auto &folder = m_next.folders[i];
          folder.state = status.value(QStringLiteral("state")).toString();
          folder.error = status.value(QStringLiteral("error")).toString();
          if (folder.error.isEmpty()) {
            folder.error = status.value(QStringLiteral("invalid")).toString();
          }
          folder.globalBytes = static_cast<qint64>(status.value(QStringLiteral("globalBytes")).toDouble());
          folder.inSyncBytes = static_cast<qint64>(status.value(QStringLiteral("inSyncBytes")).toDouble());
          folder.needItems = static_cast<qint64>(status.value(QStringLiteral("needTotalItems")).toDouble());
          folder.errorCount = status.value(QStringLiteral("errors")).toInt() +
                              status.value(QStringLiteral("pullErrors")).toInt();
        }
        replyDone(round);
      });
    }

    for (int i = 0; i < m_next.peers.size(); i++) {
      expectReply(round);
      m_client->completion(m_next.peers[i].id, {}, [this, round, i](const SyncthingClient::Reply &reply) {
        if (round != m_round) {
          return;
        }
        if (reply.ok && i < m_next.peers.size()) {
          m_next.peers[i].completion = reply.object().value(QStringLiteral("completion")).toDouble(100.0);
        }
        replyDone(round);
      });
    }

    expectReply(round);
    m_client->systemErrors([this, round](const SyncthingClient::Reply &reply) {
      if (round != m_round) {
        return;
      }
      if (reply.ok) {
        const auto errors = reply.object().value(QStringLiteral("errors")).toArray();
        if (!errors.isEmpty()) {
          m_next.lastError = errors.last().toObject().value(QStringLiteral("message")).toString();
        }
      }
      replyDone(round);
    });
  });
}

void SyncthingMonitor::expectReply(int round)
{
  if (round == m_round) {
    m_outstanding++;
  }
}

void SyncthingMonitor::replyDone(int round)
{
  if (round != m_round) {
    return;
  }
  m_outstanding--;
  if (m_outstanding <= 0) {
    finishRound(round);
  }
}

void SyncthingMonitor::finishRound(int round)
{
  if (round != m_round) {
    return;
  }
  m_inFlight = false;
  m_next.overall = deriveOverall(m_next);
  m_next.progressPercent = deriveProgress(m_next);
  m_summary = m_next;
  Q_EMIT summaryChanged(m_summary);
  m_timer.start(m_summary.apiReachable ? kPollIntervalMs : kUnreachablePollIntervalMs);
}

SyncSummary::Overall SyncthingMonitor::deriveOverall(const SyncSummary &summary)
{
  using Overall = SyncSummary::Overall;

  if (!summary.apiReachable) {
    return Overall::Unavailable;
  }
  if (summary.peers.isEmpty()) {
    return Overall::NoPeer;
  }

  bool allPeersPaused = true;
  for (const auto &peer : summary.peers) {
    allPeersPaused = allPeersPaused && peer.paused;
  }
  bool allFoldersPaused = !summary.folders.isEmpty();
  for (const auto &folder : summary.folders) {
    allFoldersPaused = allFoldersPaused && folder.paused;
  }
  if (allPeersPaused || allFoldersPaused) {
    return Overall::Paused;
  }

  bool syncing = false;
  bool scanning = false;
  for (const auto &folder : summary.folders) {
    if (folder.paused) {
      continue;
    }
    if (folder.state == QStringLiteral("error") || !folder.error.isEmpty() || folder.errorCount > 0) {
      return Overall::OutOfSync;
    }
    syncing = syncing || isSyncingState(folder.state);
    scanning = scanning || isScanningState(folder.state);
  }

  if (!summary.anyPeerConnected()) {
    return Overall::PeerOffline;
  }
  for (const auto &peer : summary.peers) {
    if (peer.connected && !peer.paused && peer.completion < 100.0) {
      syncing = true;
    }
  }
  for (const auto &folder : summary.folders) {
    if (!folder.paused && folder.state == QStringLiteral("idle") && folder.needItems > 0) {
      return Overall::OutOfSync;
    }
  }

  if (syncing) {
    return Overall::Syncing;
  }
  if (scanning) {
    return Overall::Scanning;
  }
  return Overall::Idle;
}

int SyncthingMonitor::deriveProgress(const SyncSummary &summary)
{
  double percent = 100.0;
  for (const auto &folder : summary.folders) {
    if (folder.paused || folder.globalBytes <= 0) {
      continue;
    }
    const double local = 100.0 * static_cast<double>(folder.inSyncBytes) / static_cast<double>(folder.globalBytes);
    percent = qMin(percent, local);
  }
  for (const auto &peer : summary.peers) {
    if (peer.connected && !peer.paused) {
      percent = qMin(percent, peer.completion);
    }
  }
  return qBound(0, static_cast<int>(percent + 0.5), 100);
}

} // namespace deskflow::gui::sync
