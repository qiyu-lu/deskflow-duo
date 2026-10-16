/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/Settings.h"
#include "gui/sync/SyncSummary.h"
#include "gui/sync/SyncthingProcess.h"

#include <QWidget>

class QCheckBox;
class QGroupBox;
class QLabel;
class QMenu;
class QProgressBar;
class QPushButton;
class QStackedWidget;
class QToolButton;

/**
 * @brief The simplified home page: what is paired, whether input sharing and file sync work,
 * and the few controls that are used every day. Everything else lives in the full interface.
 */
class OverviewPage : public QWidget
{
  Q_OBJECT

public:
  struct InputStatus
  {
    bool isServer = false;
    bool running = false;
    bool connected = false;
    bool canStart = false;
    QString statusText;
    bool mouseChecked = false;
    bool mouseEnabled = false;
    QString mouseToolTip;
    bool keyboardChecked = false;
    bool keyboardEnabled = false;
    QString keyboardToolTip;
    bool targetsEnabled = false;
    QString targetsText;
  };

  explicit OverviewPage(QWidget *parent = nullptr);

  void setIdentity(const QString &computerName, const QString &peerName, Settings::CoreMode mode, bool paired);
  void setInputStatus(const InputStatus &status);
  void setSyncState(
      const deskflow::gui::sync::SyncSummary &summary, deskflow::gui::sync::SyncthingProcess::State processState,
      bool syncEnabled
  );
  void setSyncFolder(const QString &path);
  void setBroadcastTargetsMenu(QMenu *menu);

Q_SIGNALS:
  void pairRequested();
  void startStopRequested();
  void mouseBroadcastToggled(bool enabled);
  void keyboardBroadcastToggled(bool enabled);
  void openFolderRequested();
  void pauseSyncRequested(bool paused);
  void openSyncthingUiRequested();
  void showFullUiRequested();

protected:
  void changeEvent(QEvent *event) override;

private:
  void buildWelcome();
  void buildDashboard();
  void updateText();
  static QString bytesText(qint64 bytes);

  QStackedWidget *m_stack = nullptr;

  // welcome
  QLabel *m_welcomeTitle = nullptr;
  QLabel *m_welcomeText = nullptr;
  QPushButton *m_btnWelcomePair = nullptr;
  QPushButton *m_btnWelcomeFullUi = nullptr;

  // dashboard header
  QLabel *m_lblComputer = nullptr;
  QLabel *m_lblPeer = nullptr;
  QPushButton *m_btnPair = nullptr;

  // input card
  QGroupBox *m_inputCard = nullptr;
  QLabel *m_lblInputIcon = nullptr;
  QLabel *m_lblInputStatus = nullptr;
  QWidget *m_broadcastRow = nullptr;
  QLabel *m_lblBroadcast = nullptr;
  QCheckBox *m_checkMouse = nullptr;
  QCheckBox *m_checkKeyboard = nullptr;
  QToolButton *m_btnTargets = nullptr;
  QPushButton *m_btnStartStop = nullptr;

  // sync card
  QGroupBox *m_syncCard = nullptr;
  QLabel *m_lblSyncIcon = nullptr;
  QLabel *m_lblSyncStatus = nullptr;
  QProgressBar *m_syncProgress = nullptr;
  QLabel *m_lblFolderCaption = nullptr;
  QLabel *m_lblFolder = nullptr;
  QPushButton *m_btnOpenFolder = nullptr;
  QPushButton *m_btnPause = nullptr;
  QPushButton *m_btnSyncthingUi = nullptr;
  QLabel *m_lblSyncError = nullptr;

  QPushButton *m_btnFullUi = nullptr;

  bool m_syncPaused = false;
  bool m_isServer = false;
  bool m_running = false;
  QString m_folderPath;
};
