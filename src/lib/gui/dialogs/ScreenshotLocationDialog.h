/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/capture/ScreenshotLocation.h"
#include "gui/sync/SyncSummary.h"

#include <QDialog>

class QComboBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;

/**
 * @brief Picks where the screenshot hotkey saves images: one of the synced folders and a path inside it.
 *
 * Only folders that send their changes to another computer are offered, so whatever is chosen here gets synced.
 * Moving a synced folder itself stays in the Syncthing web interface, the location simply follows the folder.
 */
class ScreenshotLocationDialog : public QDialog
{
  Q_OBJECT

public:
  ScreenshotLocationDialog(
      const deskflow::gui::sync::SyncSummary &summary, const deskflow::gui::ScreenshotLocation &current,
      QWidget *parent = nullptr
  );

  QString folderId() const;
  QString folderPath() const;
  QString subfolder() const; //!< normalised, empty for the folder itself

private:
  void addFolder(const QString &id, const QString &text, const QString &path, bool eligible);
  void updateState();

  deskflow::gui::sync::SyncSummary m_summary;
  bool m_canListFolders = false;
  QComboBox *m_folders = nullptr;
  QLineEdit *m_subfolder = nullptr;
  QLabel *m_preview = nullptr;
  QLabel *m_status = nullptr;
  QDialogButtonBox *m_buttons = nullptr;
};
