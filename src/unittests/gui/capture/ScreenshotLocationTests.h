/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class ScreenshotLocationTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void normalizeSubfolder_data();
  void normalizeSubfolder();
  void storedSubfolder();
  void expandPath();
  void resolveSyncedFolder();
  void resolveFolderRoot();
  void resolveNotShared();
  void resolveMissingFolder();
  void resolveWhileSyncUnavailable();
  void resolveInvalidStoredSubfolder();
  void eligibleFolders();
};
