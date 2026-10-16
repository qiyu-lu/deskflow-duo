/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "SyncSummary.h"
#include "SyncthingClient.h"

#include <QObject>
#include <QTimer>

namespace deskflow::gui::sync {

/**
 * @brief Periodically polls the Syncthing REST API and condenses the result into a SyncSummary.
 *
 * Polling is cheap for a two-computer setup: a handful of small GET requests every few seconds.
 * The event stream is intentionally not used, it would add long-polling complexity for no gain.
 */
class SyncthingMonitor : public QObject
{
  Q_OBJECT

public:
  explicit SyncthingMonitor(SyncthingClient *client, QObject *parent = nullptr);
  ~SyncthingMonitor() override;

  void setActive(bool active);
  void refreshNow();
  const SyncSummary &summary() const
  {
    return m_summary;
  }

Q_SIGNALS:
  void summaryChanged(const deskflow::gui::sync::SyncSummary &summary);

private:
  void poll();
  void gatherDetails(int round);
  void finishRound(int round);
  void expectReply(int round);
  void replyDone(int round);
  static SyncSummary::Overall deriveOverall(const SyncSummary &summary);
  static int deriveProgress(const SyncSummary &summary);

  SyncthingClient *m_client;
  QTimer m_timer;
  SyncSummary m_summary;
  SyncSummary m_next;
  int m_round = 0;
  int m_outstanding = 0;
  bool m_inFlight = false;
};

} // namespace deskflow::gui::sync
