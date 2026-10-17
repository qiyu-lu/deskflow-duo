/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenshotLocationDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

using deskflow::gui::ScreenshotLocation;
using deskflow::gui::sync::SyncSummary;

namespace {
const int kIdRole = Qt::UserRole;
const int kPathRole = Qt::UserRole + 1;
const int kEligibleRole = Qt::UserRole + 2;
const int kMinimumWidth = 520;
} // namespace

ScreenshotLocationDialog::ScreenshotLocationDialog(
    const SyncSummary &summary, const ScreenshotLocation &current, QWidget *parent
)
    : QDialog(parent),
      m_summary{summary},
      m_canListFolders{summary.apiReachable}
{
  setWindowTitle(tr("Screenshot location"));
  setMinimumWidth(kMinimumWidth);

  auto *layout = new QVBoxLayout(this);

  auto *intro = new QLabel(
      tr("Screenshots are saved inside a synced folder, so they reach the other computer by themselves. "
         "To move a synced folder, use the advanced sync settings; this location follows the folder."),
      this
  );
  intro->setWordWrap(true);
  layout->addWidget(intro);

  auto *form = new QFormLayout();
  m_folders = new QComboBox(this);
  m_folders->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
  m_folders->setMinimumContentsLength(30);
  form->addRow(tr("Synced folder:"), m_folders);

  m_subfolder = new QLineEdit(this);
  m_subfolder->setPlaceholderText(tr("Leave empty to save directly in the synced folder"));
  m_subfolder->setText(current.subfolder);
  form->addRow(tr("Subfolder:"), m_subfolder);
  layout->addLayout(form);

  m_preview = new QLabel(this);
  m_preview->setWordWrap(true);
  m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(m_preview);

  m_status = new QLabel(this);
  m_status->setWordWrap(true);
  layout->addWidget(m_status);

  layout->addStretch(1);
  m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  layout->addWidget(m_buttons);

  if (m_canListFolders) {
    const auto folders = ScreenshotLocation::eligibleFolders(summary);
    for (const auto &folder : folders) {
      addFolder(folder.id, folder.label.isEmpty() ? folder.id : folder.label, folder.path, true);
    }
    if (current.status == ScreenshotLocation::Status::NotShared && m_folders->findData(current.folderId) < 0) {
      // keep showing the current folder, so it is clear why it does not sync
      addFolder(current.folderId, tr("%1 (not synced)").arg(current.folderLabel), current.folderPath, false);
    }
  } else if (!current.folderId.isEmpty() && !current.folderPath.isEmpty()) {
    // file sync is not running: only the current folder can be kept, at its last known path
    addFolder(
        current.folderId, current.folderLabel.isEmpty() ? current.folderId : current.folderLabel, current.folderPath,
        true
    );
    m_folders->setEnabled(false);
  }

  const int currentIndex = m_folders->findData(current.folderId);
  if (currentIndex >= 0) {
    m_folders->setCurrentIndex(currentIndex);
  }
  m_folders->setEnabled(m_folders->isEnabled() && m_folders->count() > 0);

  connect(m_folders, &QComboBox::currentIndexChanged, this, &ScreenshotLocationDialog::updateState);
  connect(m_subfolder, &QLineEdit::textChanged, this, &ScreenshotLocationDialog::updateState);
  updateState();
}

QString ScreenshotLocationDialog::folderId() const
{
  return m_folders->currentData(kIdRole).toString();
}

QString ScreenshotLocationDialog::folderPath() const
{
  return m_folders->currentData(kPathRole).toString();
}

QString ScreenshotLocationDialog::subfolder() const
{
  QString normalized;
  ScreenshotLocation::normalizeSubfolder(m_subfolder->text(), normalized);
  return normalized;
}

void ScreenshotLocationDialog::addFolder(const QString &id, const QString &text, const QString &path, bool eligible)
{
  m_folders->addItem(tr("%1 · %2").arg(text, QDir::toNativeSeparators(path)), id);
  const int index = m_folders->count() - 1;
  m_folders->setItemData(index, path, kPathRole);
  m_folders->setItemData(index, eligible, kEligibleRole);
  m_folders->setItemData(index, QDir::toNativeSeparators(path), Qt::ToolTipRole);
}

void ScreenshotLocationDialog::updateState()
{
  using SubfolderError = ScreenshotLocation::SubfolderError;

  QString normalized;
  const auto error = ScreenshotLocation::normalizeSubfolder(m_subfolder->text(), normalized);
  const int index = m_folders->currentIndex();
  const bool hasFolder = index >= 0;
  const bool eligible = hasFolder && m_folders->itemData(index, kEligibleRole).toBool();

  bool ok = false;
  QString status;
  if (error == SubfolderError::Absolute) {
    status = tr("Enter a folder inside the synced folder, for example screenshots or notes/screenshots.");
  } else if (error == SubfolderError::ParentReference) {
    status = tr("The subfolder has to stay inside the synced folder, so \"..\" is not allowed.");
  } else if (error == SubfolderError::InvalidCharacter) {
    status = tr("Folder names cannot contain any of these characters: %1").arg(QStringLiteral("< > : \" | ? *"));
  } else if (!hasFolder) {
    status = m_canListFolders ? tr("No folder is shared with another computer yet. Pair a computer first, "
                                   "or share a folder in the advanced sync settings.")
                              : tr("File sync is not running and no synced folder is known yet.");
  } else if (!eligible) {
    status =
        tr("This folder is not shared with another computer, or this computer only receives changes into it. "
           "Choose another folder.");
  } else if (!m_canListFolders) {
    status =
        tr("File sync is not running, so other folders cannot be listed right now. "
           "You can still change the subfolder.");
    ok = true;
  } else {
    QStringList peers;
    for (const auto &folder : std::as_const(m_summary.folders)) {
      if (folder.id == folderId()) {
        peers = ScreenshotLocation::peerNamesOf(m_summary, folder);
        break;
      }
    }
    status = tr("Screenshots will be synced to %1.").arg(QLocale().createSeparatedList(peers));
    ok = true;
  }

  const auto path = (hasFolder && error == SubfolderError::None)
                        ? ScreenshotLocation::joinPath(m_folders->itemData(index, kPathRole).toString(), normalized)
                        : QString();
  m_preview->setText(tr("Saved in: %1").arg(QDir::toNativeSeparators(path)));
  m_preview->setVisible(!path.isEmpty());

  m_status->setText(status);
  m_status->setStyleSheet(ok ? QString() : QStringLiteral("color: %1;").arg(palette().color(QPalette::Link).name()));
  m_buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
}
