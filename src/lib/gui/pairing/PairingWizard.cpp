/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "PairingWizard.h"

#include "common/Constants.h"
#include "common/Settings.h"
#include "gui/core/NetworkMonitor.h"
#include "gui/sync/SyncManager.h"

#include <QAbstractButton>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

namespace deskflow::gui::pairing {

namespace {
quint16 pairingPort()
{
  return static_cast<quint16>(Settings::value(Settings::Pairing::Port).toInt());
}
} // namespace

//
// PairingWizard
//

PairingWizard::PairingWizard(
    const PairingInfo &localInfo, ServerConfig &serverConfig, sync::SyncManager &syncManager, QWidget *parent
)
    : QWizard(parent),
      m_local(localInfo),
      m_service{new PairingService(this)},
      m_applier{new PairingApplier(serverConfig, syncManager, this)}
{
  setWindowTitle(tr("Pair a computer"));
  setWizardStyle(QWizard::ModernStyle);
  setOption(QWizard::NoBackButtonOnLastPage, true);
  setOption(QWizard::NoCancelButtonOnLastPage, true);
  setMinimumSize(560, 420);

  setPage(Page_Role, new RolePage(this));
  setPage(Page_Wait, new WaitPage(this));
  setPage(Page_Connect, new ConnectPage(this));
  setPage(Page_Confirm, new ConfirmPage(this));
  setPage(Page_Apply, new ApplyPage(this));
  setStartId(Page_Role);

  // keep the sync device id fresh while the wizard is open, the sync service may still be starting
  connect(&syncManager, &sync::SyncManager::summaryChanged, this, [this](const sync::SyncSummary &summary) {
    if (!summary.myId.isEmpty() && m_local.syncDeviceId.isEmpty()) {
      m_local.syncDeviceId = summary.myId;
      if (auto *role = qobject_cast<RolePage *>(page(Page_Role)); role && currentId() == Page_Role) {
        role->initializePage();
      }
    }
  });
}

PairingWizard::~PairingWizard()
{
  m_service->abort();
}

void PairingWizard::reject()
{
  m_service->abort();
  QWizard::reject();
}

//
// RolePage
//

RolePage::RolePage(PairingWizard *wizard) : QWizardPage(wizard), m_wizard(wizard)
{
  setTitle(tr("Which computer is this?"));
  setSubTitle(tr("Pairing runs on both computers. Pick the role of this one."));

  auto *layout = new QVBoxLayout(this);
  layout->setSpacing(12);

  m_rbServer = new QRadioButton(tr("The keyboard and mouse are attached to this computer"), this);
  m_rbClient = new QRadioButton(tr("This computer is controlled from the other one"), this);
  m_rbServer->setChecked(true);
  layout->addWidget(m_rbServer);
  layout->addWidget(m_rbClient);

  auto *sideRow = new QHBoxLayout();
  m_lblSide = new QLabel(tr("The other computer is on my"), this);
  m_side = new QComboBox(this);
  m_side->addItem(tr("right"), false);
  m_side->addItem(tr("left"), true);
  sideRow->addSpacing(24);
  sideRow->addWidget(m_lblSide);
  sideRow->addWidget(m_side);
  sideRow->addStretch(1);
  layout->addLayout(sideRow);

  auto *name = new QLabel(
      tr("This computer is called <b>%1</b>. The name can be changed in the full interface.")
          .arg(Settings::value(Settings::Core::ComputerName).toString().toHtmlEscaped()),
      this
  );
  name->setWordWrap(true);
  layout->addSpacing(8);
  layout->addWidget(name);

  m_lblSyncHint = new QLabel(this);
  m_lblSyncHint->setWordWrap(true);
  layout->addWidget(m_lblSyncHint);
  layout->addStretch(1);

  auto updateSide = [this] {
    m_lblSide->setEnabled(m_rbServer->isChecked());
    m_side->setEnabled(m_rbServer->isChecked());
  };
  connect(m_rbServer, &QRadioButton::toggled, this, updateSide);
  updateSide();
}

void RolePage::updateSyncHint()
{
  if (m_wizard->localInfo().syncDeviceId.isEmpty()) {
    m_lblSyncHint->setText(tr("File sync is not ready yet. If it stays that way, pairing sets up keyboard and mouse "
                              "sharing only and you can pair again later for the shared folder."));
  } else {
    m_lblSyncHint->setText(tr("File sync is ready. The folder %1 will be shared.").arg(m_wizard->localInfo().syncFolderPath));
  }
}

void RolePage::initializePage()
{
  updateSyncHint();
}

int RolePage::nextId() const
{
  return m_rbServer->isChecked() ? PairingWizard::Page_Wait : PairingWizard::Page_Connect;
}

bool RolePage::validatePage()
{
  auto &local = m_wizard->mutableLocalInfo();
  local.isServer = m_rbServer->isChecked();
  local.clientOnLeft = m_side->currentData().toBool();
  updateSyncHint();
  m_wizard->service()->setLocalInfo(local);
  return true;
}

//
// WaitPage (server side)
//

WaitPage::WaitPage(PairingWizard *wizard) : QWizardPage(wizard), m_wizard(wizard)
{
  setTitle(tr("Waiting for the other computer"));
  setSubTitle(tr("On the other computer choose \"controlled from the other one\" and enter this address."));

  auto *layout = new QVBoxLayout(this);
  layout->setSpacing(12);

  m_lblAddresses = new QLabel(this);
  m_lblAddresses->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto font = m_lblAddresses->font();
  font.setPointSizeF(font.pointSizeF() * 1.5);
  font.setBold(true);
  m_lblAddresses->setFont(font);
  layout->addWidget(m_lblAddresses);

  m_lblStatus = new QLabel(this);
  m_lblStatus->setWordWrap(true);
  layout->addWidget(m_lblStatus);
  layout->addStretch(1);
}

void WaitPage::initializePage()
{
  m_exchanged = false;
  const auto addresses = NetworkMonitor::validAddresses();
  if (addresses.isEmpty()) {
    m_lblAddresses->setText(tr("No network address found"));
  } else {
    m_lblAddresses->setText(addresses.join(QStringLiteral("\n")));
  }

  auto *service = m_wizard->service();
  disconnect(service, nullptr, this, nullptr);
  connect(service, &PairingService::exchanged, this, [this](const PairingInfo &remote, const QString &) {
    m_exchanged = true;
    m_lblStatus->setText(tr("%1 is here.").arg(remote.computerName));
    Q_EMIT completeChanged();
    m_wizard->next();
  });
  connect(service, &PairingService::failed, this, [this](const QString &error) {
    m_lblStatus->setText(tr("<span style=\"color:#c0392b;\">%1</span><br/>Go back and try again.").arg(error.toHtmlEscaped()));
  });

  if (service->listen(pairingPort())) {
    m_lblStatus->setText(tr("Listening on port %1. Keep this window open until the other computer shows up...")
                             .arg(pairingPort()));
  }
}

void WaitPage::cleanupPage()
{
  m_wizard->service()->abort();
}

bool WaitPage::isComplete() const
{
  return m_exchanged;
}

int WaitPage::nextId() const
{
  return PairingWizard::Page_Confirm;
}

//
// ConnectPage (client side)
//

ConnectPage::ConnectPage(PairingWizard *wizard) : QWizardPage(wizard), m_wizard(wizard)
{
  setTitle(tr("Connect to the other computer"));
  setSubTitle(tr("Enter the address shown on the computer with the keyboard."));

  auto *layout = new QVBoxLayout(this);
  layout->setSpacing(12);

  auto *row = new QHBoxLayout();
  m_address = new QLineEdit(this);
  m_address->setPlaceholderText(tr("for example 192.168.1.20"));
  m_btnConnect = new QPushButton(tr("Connect"), this);
  m_btnConnect->setDefault(true);
  row->addWidget(m_address, 1);
  row->addWidget(m_btnConnect);
  layout->addLayout(row);

  m_lblStatus = new QLabel(this);
  m_lblStatus->setWordWrap(true);
  layout->addWidget(m_lblStatus);
  layout->addStretch(1);

  connect(m_btnConnect, &QPushButton::clicked, this, &ConnectPage::connectNow);
  connect(m_address, &QLineEdit::returnPressed, this, &ConnectPage::connectNow);
}

void ConnectPage::initializePage()
{
  m_exchanged = false;
  m_lblStatus->clear();
  if (m_address->text().isEmpty()) {
    m_address->setText(Settings::value(Settings::Pairing::PeerAddress).toString());
  }
  m_address->setFocus();

  auto *service = m_wizard->service();
  disconnect(service, nullptr, this, nullptr);
  connect(service, &PairingService::exchanged, this, [this](const PairingInfo &remote, const QString &) {
    m_exchanged = true;
    m_btnConnect->setEnabled(true);
    m_lblStatus->setText(tr("Connected to %1.").arg(remote.computerName));
    Q_EMIT completeChanged();
    m_wizard->next();
  });
  connect(service, &PairingService::failed, this, [this](const QString &error) {
    m_btnConnect->setEnabled(true);
    m_lblStatus->setText(QStringLiteral("<span style=\"color:#c0392b;\">%1</span>").arg(error.toHtmlEscaped()));
  });
}

void ConnectPage::cleanupPage()
{
  m_wizard->service()->abort();
}

void ConnectPage::connectNow()
{
  const auto address = m_address->text().trimmed();
  if (address.isEmpty()) {
    m_lblStatus->setText(tr("Enter the address first."));
    return;
  }
  m_btnConnect->setEnabled(false);
  m_lblStatus->setText(tr("Connecting to %1...").arg(address));
  m_wizard->service()->connectToServer(address, pairingPort());
}

bool ConnectPage::isComplete() const
{
  return m_exchanged;
}

int ConnectPage::nextId() const
{
  return PairingWizard::Page_Confirm;
}

//
// ConfirmPage
//

ConfirmPage::ConfirmPage(PairingWizard *wizard) : QWizardPage(wizard), m_wizard(wizard)
{
  setTitle(tr("Confirm the pairing"));
  setSubTitle(tr("Both computers show a six digit number."));

  auto *layout = new QVBoxLayout(this);
  layout->setSpacing(12);

  m_lblIntro = new QLabel(this);
  m_lblIntro->setWordWrap(true);
  layout->addWidget(m_lblIntro);

  m_lblCode = new QLabel(this);
  m_lblCode->setAlignment(Qt::AlignCenter);
  m_lblCode->setTextInteractionFlags(Qt::TextSelectableByMouse);
  auto font = m_lblCode->font();
  font.setPointSizeF(font.pointSizeF() * 3);
  font.setBold(true);
  font.setLetterSpacing(QFont::AbsoluteSpacing, 6);
  m_lblCode->setFont(font);
  layout->addWidget(m_lblCode);

  m_lblHint = new QLabel(this);
  m_lblHint->setWordWrap(true);
  m_lblHint->setAlignment(Qt::AlignCenter);
  layout->addWidget(m_lblHint);

  auto *buttons = new QHBoxLayout();
  buttons->addStretch(1);
  m_btnReject = new QPushButton(tr("These are different"), this);
  m_btnAccept = new QPushButton(tr("Same number, pair"), this);
  m_btnAccept->setDefault(true);
  buttons->addWidget(m_btnReject);
  buttons->addWidget(m_btnAccept);
  buttons->addStretch(1);
  layout->addLayout(buttons);
  layout->addStretch(1);

  connect(m_btnAccept, &QPushButton::clicked, this, [this] {
    m_btnAccept->setEnabled(false);
    m_btnReject->setEnabled(false);
    m_wizard->service()->confirm(true);
  });
  connect(m_btnReject, &QPushButton::clicked, this, [this] {
    m_wizard->service()->confirm(false);
    m_wizard->back();
  });
}

void ConfirmPage::initializePage()
{
  m_accepted = false;
  auto *service = m_wizard->service();
  const auto &remote = service->remoteInfo();
  const bool isServer = m_wizard->localInfo().isServer;

  m_lblCode->setText(service->confirmationCode());
  m_lblIntro->setText(tr("<b>%1</b> wants to pair with this computer.").arg(remote.computerName.toHtmlEscaped()));
  m_btnAccept->setVisible(isServer);
  m_btnReject->setVisible(isServer);
  m_btnAccept->setEnabled(true);
  m_btnReject->setEnabled(true);
  if (isServer) {
    m_lblHint->setText(tr("The other computer shows a number too. Pair only if both numbers are the same."));
  } else {
    m_lblHint->setText(tr("Compare this number with the one on the other computer, then confirm the pairing there."));
  }

  disconnect(service, nullptr, this, nullptr);
  connect(service, &PairingService::finished, this, [this](bool accepted) {
    if (accepted) {
      m_accepted = true;
      Q_EMIT completeChanged();
      m_wizard->next();
    } else {
      QMessageBox::information(
          m_wizard, m_wizard->windowTitle(), tr("The pairing was declined on the other computer.")
      );
      m_wizard->restart();
    }
  });
  connect(service, &PairingService::failed, this, [this](const QString &error) {
    QMessageBox::warning(m_wizard, m_wizard->windowTitle(), error);
    m_wizard->restart();
  });
}

bool ConfirmPage::isComplete() const
{
  return m_accepted;
}

int ConfirmPage::nextId() const
{
  return PairingWizard::Page_Apply;
}

//
// ApplyPage
//

ApplyPage::ApplyPage(PairingWizard *wizard) : QWizardPage(wizard), m_wizard(wizard)
{
  setTitle(tr("Setting things up"));
  setSubTitle(tr("Keyboard and mouse sharing, trust and file sync are configured on this computer."));
  setFinalPage(true);

  auto *layout = new QVBoxLayout(this);
  layout->setSpacing(8);
  m_steps = new QVBoxLayout();
  m_steps->setSpacing(6);
  layout->addLayout(m_steps);
  m_lblSummary = new QLabel(this);
  m_lblSummary->setWordWrap(true);
  layout->addSpacing(12);
  layout->addWidget(m_lblSummary);
  layout->addStretch(1);
}

void ApplyPage::initializePage()
{
  if (m_started) {
    return;
  }
  m_started = true;
  m_finished = false;

  auto *applier = m_wizard->applier();
  connect(applier, &PairingApplier::stepStarted, this, [this](const QString &title) {
    m_currentStep = new QLabel(tr("… %1").arg(title), this);
    m_steps->addWidget(m_currentStep);
  });
  connect(applier, &PairingApplier::stepFinished, this, [this](const QString &title, bool ok, const QString &detail) {
    if (m_currentStep) {
      m_currentStep->setText(QStringLiteral("%1 %2 — %3").arg(ok ? QStringLiteral("✔") : QStringLiteral("✖"), title, detail));
      m_currentStep = nullptr;
    }
  });
  connect(applier, &PairingApplier::finished, this, [this](bool, const QString &summary) {
    m_finished = true;
    m_lblSummary->setText(summary);
    Q_EMIT completeChanged();
  });

  applier->apply(m_wizard->localInfo(), m_wizard->remoteInfo(), m_wizard->remoteAddress());
}

bool ApplyPage::isComplete() const
{
  return m_finished;
}

int ApplyPage::nextId() const
{
  return -1;
}

} // namespace deskflow::gui::pairing
