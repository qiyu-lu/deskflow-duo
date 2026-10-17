/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OverviewPage.h"

#include "common/Constants.h"
#include "gui/capture/ScreenshotService.h"

#include <QCheckBox>
#include <QDir>
#include <QEvent>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QProgressBar>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

using namespace deskflow::gui::sync;
using deskflow::gui::ScreenshotLocation;
using deskflow::gui::ScreenshotService;

namespace {
const int kPageMargin = 18;
const int kCardSpacing = 14;

QLabel *makeIconLabel(QWidget *parent)
{
  auto *label = new QLabel(parent);
  label->setFixedSize(22, 22);
  label->setScaledContents(true);
  return label;
}

void setIcon(QLabel *label, const QString &themeName, QStyle::StandardPixmap fallback)
{
  const auto icon = QIcon::fromTheme(themeName, label->style()->standardIcon(fallback));
  label->setPixmap(icon.pixmap(22, 22));
}
} // namespace

OverviewPage::OverviewPage(QWidget *parent) : QWidget(parent), m_stack{new QStackedWidget(this)}
{
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->addWidget(m_stack);

  buildWelcome();
  buildDashboard();
  updateText();
}

void OverviewPage::buildWelcome()
{
  auto *page = new QWidget(m_stack);
  auto *layout = new QVBoxLayout(page);
  layout->setContentsMargins(kPageMargin * 2, kPageMargin * 2, kPageMargin * 2, kPageMargin * 2);
  layout->setSpacing(kCardSpacing);
  layout->addStretch(2);

  auto *icon = new QLabel(page);
  icon->setAlignment(Qt::AlignCenter);
  icon->setPixmap(QIcon::fromTheme(kRevFqdnName).pixmap(96, 96));
  layout->addWidget(icon);

  m_welcomeTitle = new QLabel(page);
  m_welcomeTitle->setAlignment(Qt::AlignCenter);
  auto titleFont = m_welcomeTitle->font();
  titleFont.setPointSizeF(titleFont.pointSizeF() * 1.6);
  titleFont.setBold(true);
  m_welcomeTitle->setFont(titleFont);
  layout->addWidget(m_welcomeTitle);

  m_welcomeText = new QLabel(page);
  m_welcomeText->setAlignment(Qt::AlignCenter);
  m_welcomeText->setWordWrap(true);
  layout->addWidget(m_welcomeText);

  layout->addSpacing(kCardSpacing);

  m_btnWelcomePair = new QPushButton(page);
  m_btnWelcomePair->setIcon(QIcon::fromTheme(QStringLiteral("network-connect")));
  m_btnWelcomePair->setMinimumHeight(44);
  m_btnWelcomePair->setDefault(true);
  auto buttonFont = m_btnWelcomePair->font();
  buttonFont.setPointSizeF(buttonFont.pointSizeF() * 1.15);
  m_btnWelcomePair->setFont(buttonFont);
  layout->addWidget(m_btnWelcomePair, 0, Qt::AlignHCenter);
  connect(m_btnWelcomePair, &QPushButton::clicked, this, &OverviewPage::pairRequested);

  m_btnWelcomeFullUi = new QPushButton(page);
  m_btnWelcomeFullUi->setFlat(true);
  m_btnWelcomeFullUi->setCursor(Qt::PointingHandCursor);
  layout->addWidget(m_btnWelcomeFullUi, 0, Qt::AlignHCenter);
  connect(m_btnWelcomeFullUi, &QPushButton::clicked, this, &OverviewPage::showFullUiRequested);

  layout->addStretch(3);
  m_stack->addWidget(page);
}

void OverviewPage::buildDashboard()
{
  auto *page = new QWidget(m_stack);
  auto *layout = new QVBoxLayout(page);
  layout->setContentsMargins(kPageMargin, kPageMargin, kPageMargin, kPageMargin);
  layout->setSpacing(kCardSpacing);

  // header: this computer, the paired one, pair button
  auto *header = new QHBoxLayout();
  auto *identity = new QVBoxLayout();
  identity->setSpacing(2);
  m_lblComputer = new QLabel(page);
  auto computerFont = m_lblComputer->font();
  computerFont.setPointSizeF(computerFont.pointSizeF() * 1.3);
  computerFont.setBold(true);
  m_lblComputer->setFont(computerFont);
  m_lblPeer = new QLabel(page);
  identity->addWidget(m_lblComputer);
  identity->addWidget(m_lblPeer);
  header->addLayout(identity, 1);

  m_btnPair = new QPushButton(page);
  m_btnPair->setIcon(QIcon::fromTheme(QStringLiteral("network-connect")));
  connect(m_btnPair, &QPushButton::clicked, this, &OverviewPage::pairRequested);
  header->addWidget(m_btnPair, 0, Qt::AlignTop);
  layout->addLayout(header);

  // keyboard and mouse card
  m_inputCard = new QGroupBox(page);
  auto *inputLayout = new QVBoxLayout(m_inputCard);
  inputLayout->setSpacing(10);

  auto *inputStatusRow = new QHBoxLayout();
  m_lblInputIcon = makeIconLabel(m_inputCard);
  m_lblInputStatus = new QLabel(m_inputCard);
  m_lblInputStatus->setWordWrap(true);
  inputStatusRow->addWidget(m_lblInputIcon);
  inputStatusRow->addWidget(m_lblInputStatus, 1);
  m_btnStartStop = new QPushButton(m_inputCard);
  connect(m_btnStartStop, &QPushButton::clicked, this, &OverviewPage::startStopRequested);
  inputStatusRow->addWidget(m_btnStartStop);
  inputLayout->addLayout(inputStatusRow);

  m_broadcastRow = new QWidget(m_inputCard);
  auto *broadcastLayout = new QHBoxLayout(m_broadcastRow);
  broadcastLayout->setContentsMargins(0, 0, 0, 0);
  m_lblBroadcast = new QLabel(m_broadcastRow);
  m_checkMouse = new QCheckBox(m_broadcastRow);
  m_checkKeyboard = new QCheckBox(m_broadcastRow);
  m_btnTargets = new QToolButton(m_broadcastRow);
  m_btnTargets->setPopupMode(QToolButton::InstantPopup);
  m_btnTargets->setToolButtonStyle(Qt::ToolButtonTextOnly);
  broadcastLayout->addWidget(m_lblBroadcast);
  broadcastLayout->addWidget(m_checkMouse);
  broadcastLayout->addWidget(m_checkKeyboard);
  broadcastLayout->addWidget(m_btnTargets);
  broadcastLayout->addStretch(1);
  connect(m_checkMouse, &QCheckBox::clicked, this, &OverviewPage::mouseBroadcastToggled);
  connect(m_checkKeyboard, &QCheckBox::clicked, this, &OverviewPage::keyboardBroadcastToggled);
  inputLayout->addWidget(m_broadcastRow);
  layout->addWidget(m_inputCard);

  // file sync card
  m_syncCard = new QGroupBox(page);
  auto *syncLayout = new QVBoxLayout(m_syncCard);
  syncLayout->setSpacing(10);

  auto *syncStatusRow = new QHBoxLayout();
  m_lblSyncIcon = makeIconLabel(m_syncCard);
  m_lblSyncStatus = new QLabel(m_syncCard);
  m_lblSyncStatus->setWordWrap(true);
  syncStatusRow->addWidget(m_lblSyncIcon);
  syncStatusRow->addWidget(m_lblSyncStatus, 1);
  m_btnPause = new QPushButton(m_syncCard);
  connect(m_btnPause, &QPushButton::clicked, this, [this] { Q_EMIT pauseSyncRequested(!m_syncPaused); });
  syncStatusRow->addWidget(m_btnPause);
  syncLayout->addLayout(syncStatusRow);

  m_syncProgress = new QProgressBar(m_syncCard);
  m_syncProgress->setRange(0, 100);
  m_syncProgress->setTextVisible(true);
  m_syncProgress->setVisible(false);
  syncLayout->addWidget(m_syncProgress);

  // the shared folder and, on the keyboard and mouse side, where screenshots go; a grid keeps them aligned
  auto *folderGrid = new QGridLayout();
  folderGrid->setColumnStretch(1, 1);
  m_lblFolderCaption = new QLabel(m_syncCard);
  m_lblFolder = new QLabel(m_syncCard);
  m_lblFolder->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_lblFolder->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_btnOpenFolder = new QPushButton(m_syncCard);
  m_btnOpenFolder->setIcon(QIcon::fromTheme(QStringLiteral("document-open")));
  connect(m_btnOpenFolder, &QPushButton::clicked, this, &OverviewPage::openFolderRequested);
  folderGrid->addWidget(m_lblFolderCaption, 0, 0);
  folderGrid->addWidget(m_lblFolder, 0, 1);
  folderGrid->addWidget(m_btnOpenFolder, 0, 3);

  m_lblScreenshotCaption = new QLabel(m_syncCard);
  m_lblScreenshot = new QLabel(m_syncCard);
  m_lblScreenshot->setTextInteractionFlags(Qt::TextSelectableByMouse);
  m_lblScreenshot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  m_btnChangeScreenshot = new QPushButton(m_syncCard);
  m_btnChangeScreenshot->setIcon(QIcon::fromTheme(QStringLiteral("document-edit")));
  connect(m_btnChangeScreenshot, &QPushButton::clicked, this, &OverviewPage::changeScreenshotLocationRequested);
  m_btnOpenScreenshot = new QPushButton(m_syncCard);
  m_btnOpenScreenshot->setIcon(QIcon::fromTheme(QStringLiteral("document-open")));
  connect(m_btnOpenScreenshot, &QPushButton::clicked, this, &OverviewPage::openScreenshotFolderRequested);
  folderGrid->addWidget(m_lblScreenshotCaption, 1, 0);
  folderGrid->addWidget(m_lblScreenshot, 1, 1);
  folderGrid->addWidget(m_btnChangeScreenshot, 1, 2);
  folderGrid->addWidget(m_btnOpenScreenshot, 1, 3);

  syncLayout->addLayout(folderGrid);

  // outside the grid: a word-wrapped label spanning grid columns does not get the height it needs
  m_lblScreenshotWarning = new QLabel(m_syncCard);
  m_lblScreenshotWarning->setWordWrap(true);
  m_lblScreenshotWarning->setVisible(false);
  syncLayout->addWidget(m_lblScreenshotWarning);

  m_lblSyncError = new QLabel(m_syncCard);
  m_lblSyncError->setWordWrap(true);
  m_lblSyncError->setVisible(false);
  syncLayout->addWidget(m_lblSyncError);

  auto *syncButtons = new QHBoxLayout();
  syncButtons->addStretch(1);
  m_btnSyncthingUi = new QPushButton(m_syncCard);
  m_btnSyncthingUi->setIcon(QIcon::fromTheme(QStringLiteral("configure")));
  connect(m_btnSyncthingUi, &QPushButton::clicked, this, &OverviewPage::openSyncthingUiRequested);
  syncButtons->addWidget(m_btnSyncthingUi);
  syncLayout->addLayout(syncButtons);
  layout->addWidget(m_syncCard);

  layout->addStretch(1);

  m_btnFullUi = new QPushButton(page);
  m_btnFullUi->setFlat(true);
  m_btnFullUi->setCursor(Qt::PointingHandCursor);
  connect(m_btnFullUi, &QPushButton::clicked, this, &OverviewPage::showFullUiRequested);
  layout->addWidget(m_btnFullUi, 0, Qt::AlignRight);

  m_stack->addWidget(page);
}

void OverviewPage::setIdentity(const QString &computerName, const QString &peerName, Settings::CoreMode mode, bool paired)
{
  m_isServer = mode == Settings::CoreMode::Server;
  const bool configured = paired || mode != Settings::CoreMode::None;
  m_stack->setCurrentIndex(configured ? 1 : 0);

  QString role;
  if (mode == Settings::CoreMode::Server) {
    role = tr("keyboard and mouse are attached here");
  } else if (mode == Settings::CoreMode::Client) {
    role = tr("controlled from the other computer");
  }
  m_lblComputer->setText(role.isEmpty() ? computerName : tr("%1 · %2").arg(computerName, role));

  if (peerName.isEmpty()) {
    m_lblPeer->setText(tr("No computer paired yet"));
  } else {
    m_lblPeer->setText(tr("Paired with %1").arg(peerName));
  }
  m_btnPair->setText(paired ? tr("Pair again...") : tr("Pair a computer..."));
  updateScreenshotRow();
}

void OverviewPage::setInputStatus(const InputStatus &status)
{
  m_running = status.running;
  m_lblInputStatus->setText(status.statusText);
  if (!status.running) {
    setIcon(m_lblInputIcon, QStringLiteral("network-disconnect"), QStyle::SP_MessageBoxWarning);
  } else if (status.connected) {
    setIcon(m_lblInputIcon, QStringLiteral("dialog-ok-apply"), QStyle::SP_DialogApplyButton);
  } else {
    setIcon(m_lblInputIcon, QStringLiteral("system-search"), QStyle::SP_BrowserReload);
  }

  m_btnStartStop->setEnabled(status.canStart || status.running);
  if (status.running) {
    m_btnStartStop->setText(status.isServer ? tr("Stop") : tr("Disconnect"));
    m_btnStartStop->setIcon(QIcon::fromTheme(QStringLiteral("process-stop")));
  } else {
    m_btnStartStop->setText(status.isServer ? tr("Start") : tr("Connect"));
    m_btnStartStop->setIcon(QIcon::fromTheme(status.isServer ? QStringLiteral("system-run") : QStringLiteral("network-connect")));
  }

  m_broadcastRow->setVisible(status.isServer);
  m_checkMouse->setChecked(status.mouseChecked);
  m_checkMouse->setEnabled(status.mouseEnabled);
  m_checkMouse->setToolTip(status.mouseToolTip);
  m_checkKeyboard->setChecked(status.keyboardChecked);
  m_checkKeyboard->setEnabled(status.keyboardEnabled);
  m_checkKeyboard->setToolTip(status.keyboardToolTip);
  m_btnTargets->setEnabled(status.targetsEnabled);
  m_btnTargets->setText(status.targetsText);
}

void OverviewPage::setSyncState(const SyncSummary &summary, SyncthingProcess::State processState, bool syncEnabled)
{
  using Overall = SyncSummary::Overall;
  using State = SyncthingProcess::State;

  QString text;
  QString iconName;
  QStyle::StandardPixmap fallback = QStyle::SP_MessageBoxInformation;
  bool showProgress = false;
  bool controlsEnabled = summary.apiReachable;
  m_syncPaused = summary.overall == Overall::Paused;

  if (!syncEnabled) {
    text = tr("File sync is turned off in the preferences.");
    iconName = QStringLiteral("dialog-information");
    controlsEnabled = false;
  } else if (processState == State::Unavailable) {
    text = tr("The Syncthing program was not found, so files are not synced.");
    iconName = QStringLiteral("dialog-warning");
    fallback = QStyle::SP_MessageBoxWarning;
    controlsEnabled = false;
  } else if (processState == State::Failed) {
    text = tr("Syncthing could not be started. See the log for details.");
    iconName = QStringLiteral("dialog-error");
    fallback = QStyle::SP_MessageBoxCritical;
    controlsEnabled = false;
  } else if (processState != State::Running || !summary.apiReachable) {
    text = tr("Starting file sync...");
    iconName = QStringLiteral("system-search");
    fallback = QStyle::SP_BrowserReload;
    controlsEnabled = false;
  } else {
    const auto peerName = summary.peers.isEmpty() ? QString() : summary.peers.first().name;
    switch (summary.overall) {
    case Overall::NoPeer:
      text = tr("No computer is set up for file sync yet. Pairing takes care of it.");
      iconName = QStringLiteral("dialog-information");
      break;
    case Overall::PeerOffline:
      text = tr("Waiting for %1 to come online.").arg(peerName);
      iconName = QStringLiteral("network-disconnect");
      fallback = QStyle::SP_MessageBoxWarning;
      break;
    case Overall::Paused:
      text = tr("File sync is paused.");
      iconName = QStringLiteral("process-stop");
      break;
    case Overall::Scanning:
      text = tr("Checking the folder for changes...");
      iconName = QStringLiteral("system-search");
      fallback = QStyle::SP_BrowserReload;
      break;
    case Overall::Syncing:
      text = tr("Syncing with %1, %2% done.").arg(peerName).arg(summary.progressPercent);
      iconName = QStringLiteral("view-refresh");
      fallback = QStyle::SP_BrowserReload;
      showProgress = true;
      break;
    case Overall::OutOfSync:
      text = tr("Some files could not be synced.");
      iconName = QStringLiteral("dialog-error");
      fallback = QStyle::SP_MessageBoxCritical;
      break;
    case Overall::Idle:
      text = tr("Everything is in sync with %1.").arg(peerName);
      iconName = QStringLiteral("dialog-ok-apply");
      fallback = QStyle::SP_DialogApplyButton;
      break;
    case Overall::Unavailable:
      text = tr("Starting file sync...");
      iconName = QStringLiteral("system-search");
      fallback = QStyle::SP_BrowserReload;
      break;
    }
  }

  m_lblSyncStatus->setText(text);
  setIcon(m_lblSyncIcon, iconName, fallback);
  m_syncProgress->setVisible(showProgress);
  m_syncProgress->setValue(summary.progressPercent);

  m_btnPause->setEnabled(controlsEnabled && !summary.peers.isEmpty());
  m_btnPause->setText(m_syncPaused ? tr("Resume") : tr("Pause"));
  m_btnPause->setIcon(QIcon::fromTheme(m_syncPaused ? QStringLiteral("system-run") : QStringLiteral("process-stop")));
  m_btnSyncthingUi->setEnabled(controlsEnabled);

  QString error = summary.lastError;
  for (const auto &folder : summary.folders) {
    if (!folder.error.isEmpty()) {
      error = folder.error;
      break;
    }
  }
  if (error.isEmpty()) {
    error = summary.listenError;
  }
  m_lblSyncError->setVisible(summary.apiReachable && !error.isEmpty());
  m_lblSyncError->setText(error);
}

void OverviewPage::setSyncFolder(const QString &path, bool notSynced)
{
  m_folderPath = path;
  m_folderNotSynced = notSynced;
  updateFolderRow();
}

void OverviewPage::updateFolderRow()
{
  const auto path = QDir::toNativeSeparators(m_folderPath);
  m_lblFolder->setText(m_folderNotSynced ? tr("%1 (not synced)").arg(path) : path);
  m_lblFolder->setToolTip(
      m_folderNotSynced ? tr("File sync does not have this folder, so its contents are not synced.") : path
  );
  m_btnOpenFolder->setEnabled(!m_folderPath.isEmpty());
}

void OverviewPage::setScreenshotLocation(const ScreenshotLocation &location, bool syncEnabled)
{
  m_screenshotLocation = location;
  m_screenshotSyncEnabled = syncEnabled;
  updateScreenshotRow();
}

void OverviewPage::updateScreenshotRow()
{
  const bool visible = m_isServer;
  m_lblScreenshotCaption->setVisible(visible);
  m_lblScreenshot->setVisible(visible);
  m_btnChangeScreenshot->setVisible(visible);
  m_btnOpenScreenshot->setVisible(visible);

  const auto &location = m_screenshotLocation;
  const auto directory = QDir::toNativeSeparators(location.directory);
  QString text;
  if (!location.folderFound()) {
    text = directory;
  } else if (location.subfolder.isEmpty()) {
    text = location.folderLabel;
  } else {
    text = tr("%1 / %2").arg(location.folderLabel, QDir::toNativeSeparators(location.subfolder));
  }
  m_lblScreenshot->setText(text.isEmpty() ? tr("not set up yet") : text);
  m_lblScreenshot->setToolTip(directory);
  m_btnOpenScreenshot->setEnabled(!location.directory.isEmpty());

  // while file sync starts up the status line above already says so, no need to repeat it here
  QString warning;
  const bool starting = location.status == ScreenshotLocation::Status::SyncUnavailable && m_screenshotSyncEnabled;
  if (!location.isSynced() && !starting) {
    const auto reason = ScreenshotService::notSyncedReason(location, m_screenshotSyncEnabled);
    if (location.status == ScreenshotLocation::Status::SyncUnavailable) {
      warning = tr("Screenshots are not synced: %1.").arg(reason);
    } else {
      warning = tr("Screenshots are not synced: %1. Use \"%2\" to pick a synced folder.")
                    .arg(reason, m_btnChangeScreenshot->text());
    }
  }
  m_lblScreenshotWarning->setText(warning);
  m_lblScreenshotWarning->setVisible(visible && !warning.isEmpty());
}

void OverviewPage::setBroadcastTargetsMenu(QMenu *menu)
{
  m_btnTargets->setMenu(menu);
}

void OverviewPage::changeEvent(QEvent *event)
{
  if (event->type() == QEvent::LanguageChange) {
    updateText();
  }
  QWidget::changeEvent(event);
}

void OverviewPage::updateText()
{
  m_welcomeTitle->setText(tr("Connect two computers"));
  m_welcomeText->setText(
      tr("One pairing sets up keyboard and mouse sharing and a synced folder. "
         "Install %1 on both computers, then start the pairing on each of them.")
          .arg(kAppName)
  );
  m_btnWelcomePair->setText(tr("Pair a computer..."));
  m_btnWelcomeFullUi->setText(tr("I already have a configuration, show the full interface"));

  m_inputCard->setTitle(tr("Keyboard and mouse"));
  m_lblBroadcast->setText(tr("Send to both:"));
  m_checkMouse->setText(tr("Mouse"));
  m_checkKeyboard->setText(tr("Keyboard"));

  m_syncCard->setTitle(tr("File sync"));
  m_lblFolderCaption->setText(tr("Shared folder:"));
  m_btnOpenFolder->setText(tr("Open"));
  m_lblScreenshotCaption->setText(tr("Screenshots go to:"));
  m_btnChangeScreenshot->setText(tr("Change..."));
  m_btnChangeScreenshot->setToolTip(tr("Choose the synced folder and subfolder for hotkey screenshots."));
  m_btnOpenScreenshot->setText(tr("Open"));
  m_btnSyncthingUi->setText(tr("Advanced sync settings..."));
  m_btnSyncthingUi->setToolTip(tr("Opens the Syncthing web interface in your browser for everything beyond the basics."));
  m_btnFullUi->setText(tr("Show full interface"));
  m_lblSyncError->setStyleSheet(QStringLiteral("color: %1;").arg(palette().color(QPalette::Link).name()));
  m_lblScreenshotWarning->setStyleSheet(m_lblSyncError->styleSheet());
  updateFolderRow();
  updateScreenshotRow();
}

QString OverviewPage::bytesText(qint64 bytes)
{
  return QLocale().formattedDataSize(bytes);
}
