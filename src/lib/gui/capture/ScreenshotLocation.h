/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "gui/sync/SyncSummary.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace deskflow::gui {

/**
 * @brief Where the screenshot hotkey saves an image, resolved against Syncthing's live configuration.
 *
 * The setting is a Syncthing folder id plus a path inside that folder, never an absolute path. Syncthing does not let
 * an existing folder change its path, so moving it means removing it and adding it again in the Syncthing web
 * interface; keeping only the id makes the location follow the folder, and a folder that disappeared is noticed
 * instead of silently writing into a directory that no longer syncs.
 *
 * Plain data and pure functions, so it can be tested without a running Syncthing.
 */
struct ScreenshotLocation
{
  enum class Status
  {
    Synced,         //!< inside a folder that sends its changes to at least one other computer
    NotShared,      //!< the folder exists but is not shared, or this computer only receives changes into it
    FolderMissing,  //!< Syncthing no longer has the configured folder
    SyncUnavailable //!< file sync is off or not reachable, so the folder cannot be checked right now
  };

  enum class SubfolderError
  {
    None,
    Absolute,         //!< an absolute path, a home directory path or a drive letter
    ParentReference,  //!< contains "..", which would leave the synced folder
    InvalidCharacter, //!< a character that Windows does not allow in file names
  };

  Status status = Status::SyncUnavailable;
  QString folderId;
  QString folderLabel;   //!< Syncthing label, or the id if the folder has none; empty when the folder is unknown
  QString folderPath;    //!< root of the folder on this computer; the cached fallback unless the folder was found
  QString subfolder;     //!< normalised path inside the folder, empty for the folder itself
  QString directory;     //!< where images are written; empty when no location is known at all
  QStringList peerNames; //!< computers the folder is shared with

  bool isSynced() const
  {
    return status == Status::Synced;
  }

  /// Whether Syncthing confirmed the folder, i.e. folderPath is its live path.
  bool folderFound() const
  {
    return status == Status::Synced || status == Status::NotShared;
  }

  /**
   * @brief Clean up a user supplied subfolder: "a\\b/./c/" becomes "a/b/c", "" and "." mean the folder itself.
   * @param normalized receives the cleaned path; only meaningful when the result is SubfolderError::None
   */
  static SubfolderError normalizeSubfolder(const QString &input, QString &normalized);

  /// The value to store for a normalised subfolder; the folder itself is stored as "." so it never reads as unset.
  static QString storedSubfolder(const QString &normalized);

  /// Whether files written into this folder are sent to another computer.
  static bool sendsChanges(const sync::SyncSummary::Folder &folder);

  /// The folders a screenshot location may point at, in Syncthing's order.
  static QList<sync::SyncSummary::Folder> eligibleFolders(const sync::SyncSummary &summary);

  /// Display names of the computers a folder is shared with.
  static QStringList peerNamesOf(const sync::SyncSummary &summary, const sync::SyncSummary::Folder &folder);

  /// Join a folder root and a normalised subfolder; empty if the root is empty.
  static QString joinPath(const QString &root, const QString &subfolder);

  /**
   * @brief Resolve the configured location.
   * @param fallbackRoot folder root to use when Syncthing cannot confirm the folder, normally its last known path
   */
  static ScreenshotLocation resolve(
      const sync::SyncSummary &summary, const QString &folderId, const QString &subfolder, const QString &fallbackRoot
  );
};

} // namespace deskflow::gui
