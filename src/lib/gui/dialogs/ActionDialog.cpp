/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ActionDialog.h"
#include "ui_ActionDialog.h"

#include "common/Action.h"
#include "common/Hotkey.h"
#include "common/KeySequence.h"
#include "config/ServerConfig.h"

#include <QTimer>
#include <array>

ActionDialog::ActionDialog(QWidget *parent, const ServerConfig &config, Hotkey &hotkey, Action &action)
    : QDialog(parent, Qt::WindowTitleHint | Qt::WindowSystemMenuHint),
      ui{std::make_unique<Ui::ActionDialog>()},
      m_hotkey(hotkey),
      m_action(action)
{
  ui->setupUi(this);

  const std::array actionTypes{
      Action::Type::keyDown,
      Action::Type::keyUp,
      Action::Type::keystroke,
      Action::Type::switchToScreen,
      Action::Type::switchInDirection,
      Action::Type::switchToNextScreen,
      Action::Type::lockCursorToScreen,
      Action::Type::restartAllConnections,
      Action::Type::mouseBroadcast,
      Action::Type::keyboardBroadcast,
      Action::Type::screenshot,
  };

  Q_ASSERT(ui->comboActionType->count() == static_cast<int>(actionTypes.size()));
  for (int i = 0; i < static_cast<int>(actionTypes.size()); ++i) {
    ui->comboActionType->setItemData(i, static_cast<int>(actionTypes.at(i)));
  }

  connect(ui->keySequenceWidget, &KeySequenceWidget::keySequenceChanged, this, &ActionDialog::keySequenceChanged);
  connect(
      ui->comboActionType, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ActionDialog::actionTypeChanged
  );
  connect(ui->listScreens, &QListWidget::itemChanged, this, &ActionDialog::itemToggled);
  connect(ui->buttonBox, &QDialogButtonBox::accepted, this, &ActionDialog::accept);
  connect(ui->buttonBox, &QDialogButtonBox::rejected, this, &ActionDialog::reject);

  ui->keySequenceWidget->setText(m_action.keySequence().toString());
  ui->keySequenceWidget->setKeySequence(m_action.keySequence());

  ui->comboSwitchInDirection->setCurrentIndex(m_action.switchDirection());
  ui->comboLockCursorToScreen->setCurrentIndex(m_action.lockCursorMode());
  ui->comboMouseBroadcastMode->setCurrentIndex(m_action.mouseBroadcastMode());
  ui->comboKeyboardBroadcastMode->setCurrentIndex(m_action.keyboardBroadcastMode());

  const int actionIndex = ui->comboActionType->findData(m_action.type());
  ui->comboActionType->setCurrentIndex(actionIndex >= 0 ? actionIndex : 0);
  ui->comboTriggerOn->setCurrentIndex(m_action.activeOnRelease());

  for (const Screen &screen : config.screens()) {
    if (screen.isNull())
      continue;
    auto *newListItem = new QListWidgetItem(screen.name());
    newListItem->setData(Qt::UserRole, screen.isServer());
    newListItem->setCheckState(Qt::Checked);
    if ((m_action.typeScreenNames().indexOf(screen.name()) == -1) &&
        (m_action.haveScreens() && !m_action.typeScreenNames().isEmpty()))
      newListItem->setCheckState(Qt::Unchecked);

    ui->listScreens->addItem(newListItem);

    ui->comboSwitchToScreen->addItem(tr("Switch to %1").arg(screen.name()), screen.name());
    if (screen.name() == m_action.switchScreenName())
      ui->comboSwitchToScreen->setCurrentIndex(ui->comboSwitchToScreen->count() - 1);
  }

  ui->keySequenceWidget->setVisible(false);
  ui->groupScreens->setVisible(false);
  ui->listScreens->setEnabled(!ui->keySequenceWidget->keySequence().isMouseButton());
  ui->comboSwitchToScreen->setVisible(false);
  ui->comboSwitchInDirection->setVisible(false);
  ui->comboLockCursorToScreen->setVisible(false);
  ui->comboMouseBroadcastMode->setVisible(false);
  ui->comboKeyboardBroadcastMode->setVisible(false);

  actionTypeChanged(ui->comboActionType->currentIndex());
}

void ActionDialog::accept()
{
  if (!canSave())
    return;

  m_action.setKeySequence(ui->keySequenceWidget->keySequence());
  const auto actionType = currentActionType();
  m_action.setType(static_cast<int>(actionType));

  m_action.clearScreens();

  int availableScreenCount = 0;
  int screenCount = 0;
  for (int i = 0; i < ui->listScreens->count(); i++) {
    const auto &item = ui->listScreens->item(i);
    if (item->isHidden()) {
      continue;
    }

    ++availableScreenCount;
    if (item->checkState() == Qt::Checked) {
      ++screenCount;
      m_action.addScreen(item->text());
    }
  }

  if (screenCount == availableScreenCount)
    m_action.clearScreens();

  m_action.setHaveScreens(screenCount);

  m_action.setSwitchScreenName(ui->comboSwitchToScreen->currentData().toString());
  m_action.setSwitchDirection(ui->comboSwitchInDirection->currentIndex());
  m_action.setLockCursorMode(ui->comboLockCursorToScreen->currentIndex());
  m_action.setMouseBroadcastMode(ui->comboMouseBroadcastMode->currentIndex());
  m_action.setKeyboardBroadcastMode(ui->comboKeyboardBroadcastMode->currentIndex());
  m_action.setActiveOnRelease(ui->comboTriggerOn->currentIndex());
  m_action.setRestartServer(actionType == Action::Type::restartAllConnections);

  QDialog::accept();
}

void ActionDialog::updateSize()
{
  setMaximumSize(QSize(16777215, 1677215));
  adjustSize();
  if (!isKeyAction(currentActionType()) && currentActionType() != Action::Type::mouseBroadcast &&
      currentActionType() != Action::Type::keyboardBroadcast)
    setMaximumSize(size());
}

void ActionDialog::keySequenceChanged()
{
  ui->listScreens->setEnabled(
      currentActionType() == Action::Type::mouseBroadcast || currentActionType() == Action::Type::keyboardBroadcast ||
      !ui->keySequenceWidget->keySequence().isMouseButton()
  );
  ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(canSave());
}

void ActionDialog::itemToggled() const
{
  ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(canSave());
}

void ActionDialog::actionTypeChanged(int index)
{
  const auto actionType = static_cast<Action::Type>(ui->comboActionType->itemData(index).toInt());
  const bool isMouseBroadcast = actionType == Action::Type::mouseBroadcast;
  const bool isKeyboardBroadcast = actionType == Action::Type::keyboardBroadcast;
  const bool isBroadcast = isMouseBroadcast || isKeyboardBroadcast;
  ui->keySequenceWidget->setVisible(isKeyAction(actionType));
  ui->groupScreens->setVisible(isKeyAction(actionType) || isBroadcast);
  ui->listScreens->setEnabled(isBroadcast || !ui->keySequenceWidget->keySequence().isMouseButton());
  for (int i = 0; i < ui->listScreens->count(); ++i) {
    auto *item = ui->listScreens->item(i);
    item->setHidden(isBroadcast && item->data(Qt::UserRole).toBool());
  }
  ui->comboSwitchToScreen->setVisible(actionType == Action::Type::switchToScreen);
  ui->comboSwitchInDirection->setVisible(actionType == Action::Type::switchInDirection);
  ui->comboLockCursorToScreen->setVisible(actionType == Action::Type::lockCursorToScreen);
  ui->comboMouseBroadcastMode->setVisible(isMouseBroadcast);
  ui->comboKeyboardBroadcastMode->setVisible(isKeyboardBroadcast);
  ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(canSave());
  QTimer::singleShot(1, this, &ActionDialog::updateSize);
}

Action::Type ActionDialog::currentActionType() const
{
  return static_cast<Action::Type>(ui->comboActionType->currentData().toInt());
}

bool ActionDialog::isKeyAction(Action::Type type) const
{
  return type == Action::Type::keyDown || type == Action::Type::keyUp || type == Action::Type::keystroke;
}

bool ActionDialog::canSave() const
{
  const auto actionType = currentActionType();
  if (isKeyAction(actionType) || actionType == Action::Type::mouseBroadcast ||
      actionType == Action::Type::keyboardBroadcast) {
    const QList<QListWidgetItem *> items = ui->listScreens->findItems("*", Qt::MatchWildcard);
    int totalChecked = 0;
    for (const auto &item : items) {
      if (!item->isHidden() && item->checkState() == Qt::Checked)
        totalChecked++;
    }
    if (actionType == Action::Type::mouseBroadcast || actionType == Action::Type::keyboardBroadcast)
      return totalChecked > 0;

    return !ui->keySequenceWidget->keySequence().toString().isEmpty() && totalChecked > 0;
  }
  return true;
}

ActionDialog::~ActionDialog() = default;
