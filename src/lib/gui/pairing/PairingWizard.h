/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "PairingApplier.h"
#include "PairingService.h"

#include <QWizard>
#include <QWizardPage>

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QVBoxLayout;
class ServerConfig;

namespace deskflow::gui::sync {
class SyncManager;
}

namespace deskflow::gui::pairing {

class RolePage;
class WaitPage;
class ConnectPage;
class ConfirmPage;
class ApplyPage;

/**
 * @brief Guided pairing of two computers: role, exchange over the LAN, numeric confirmation,
 * then applying input sharing, trust and file sync in one go.
 */
class PairingWizard : public QWizard
{
  Q_OBJECT

public:
  enum PageId
  {
    Page_Role,
    Page_Wait,
    Page_Connect,
    Page_Confirm,
    Page_Apply
  };

  PairingWizard(
      const PairingInfo &localInfo, ServerConfig &serverConfig, sync::SyncManager &syncManager,
      QWidget *parent = nullptr
  );
  ~PairingWizard() override;

  const PairingInfo &localInfo() const
  {
    return m_local;
  }
  const PairingInfo &remoteInfo() const
  {
    return m_service->remoteInfo();
  }
  QString remoteAddress() const
  {
    return m_service->remoteAddress();
  }
  PairingService *service() const
  {
    return m_service;
  }
  PairingApplier *applier() const
  {
    return m_applier;
  }
  PairingInfo &mutableLocalInfo()
  {
    return m_local;
  }
  void setSyncDeviceId(const QString &deviceId)
  {
    m_local.syncDeviceId = deviceId;
  }

protected:
  void reject() override;

private:
  PairingInfo m_local;
  PairingService *m_service;
  PairingApplier *m_applier;
};

class RolePage : public QWizardPage
{
  Q_OBJECT

public:
  explicit RolePage(PairingWizard *wizard);
  void initializePage() override;
  int nextId() const override;
  bool validatePage() override;

private:
  void updateSyncHint();

  PairingWizard *m_wizard;
  QRadioButton *m_rbServer = nullptr;
  QRadioButton *m_rbClient = nullptr;
  QComboBox *m_side = nullptr;
  QLabel *m_lblSide = nullptr;
  QLabel *m_lblSyncHint = nullptr;
};

class WaitPage : public QWizardPage
{
  Q_OBJECT

public:
  explicit WaitPage(PairingWizard *wizard);
  void initializePage() override;
  void cleanupPage() override;
  bool isComplete() const override;
  int nextId() const override;

private:
  PairingWizard *m_wizard;
  QLabel *m_lblAddresses = nullptr;
  QLabel *m_lblStatus = nullptr;
  bool m_exchanged = false;
};

class ConnectPage : public QWizardPage
{
  Q_OBJECT

public:
  explicit ConnectPage(PairingWizard *wizard);
  void initializePage() override;
  void cleanupPage() override;
  bool isComplete() const override;
  int nextId() const override;

private:
  void connectNow();

  PairingWizard *m_wizard;
  QLineEdit *m_address = nullptr;
  QPushButton *m_btnConnect = nullptr;
  QLabel *m_lblStatus = nullptr;
  bool m_exchanged = false;
};

class ConfirmPage : public QWizardPage
{
  Q_OBJECT

public:
  explicit ConfirmPage(PairingWizard *wizard);
  void initializePage() override;
  bool isComplete() const override;
  int nextId() const override;

private:
  PairingWizard *m_wizard;
  QLabel *m_lblIntro = nullptr;
  QLabel *m_lblCode = nullptr;
  QLabel *m_lblHint = nullptr;
  QPushButton *m_btnAccept = nullptr;
  QPushButton *m_btnReject = nullptr;
  bool m_accepted = false;
};

class ApplyPage : public QWizardPage
{
  Q_OBJECT

public:
  explicit ApplyPage(PairingWizard *wizard);
  void initializePage() override;
  bool isComplete() const override;
  int nextId() const override;

private:
  PairingWizard *m_wizard;
  QVBoxLayout *m_steps = nullptr;
  QLabel *m_lblSummary = nullptr;
  QLabel *m_currentStep = nullptr;
  bool m_finished = false;
  bool m_started = false;
};

} // namespace deskflow::gui::pairing
