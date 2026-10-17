/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenshotLocation.h"

#include "common/Constants.h"

#include <QDir>

namespace deskflow::gui {

using sync::SyncSummary;

namespace {
// characters Windows rejects in file names; the other computer may well run Windows
const auto kForbiddenCharacters = QStringLiteral("<>:\"|?*");
const int kShortDeviceIdLength = 7;
} // namespace

ScreenshotLocation::SubfolderError ScreenshotLocation::normalizeSubfolder(const QString &input, QString &normalized)
{
  normalized.clear();

  QString text = input.trimmed();
  text.replace(QLatin1Char('\\'), QLatin1Char('/'));
  if (text.startsWith(QLatin1Char('/')) || text == QStringLiteral("~") || text.startsWith(QStringLiteral("~/"))) {
    return SubfolderError::Absolute;
  }
  if (text.size() >= 2 && text.at(0).isLetter() && text.at(1) == QLatin1Char(':')) {
    return SubfolderError::Absolute;
  }

  QStringList parts;
  const auto rawParts = text.split(QLatin1Char('/'), Qt::SkipEmptyParts);
  for (const auto &rawPart : rawParts) {
    const auto part = rawPart.trimmed();
    if (part.isEmpty() || part == QStringLiteral(".")) {
      continue;
    }
    if (part == QStringLiteral("..")) {
      return SubfolderError::ParentReference;
    }
    for (const auto character : part) {
      if (character.unicode() < 0x20 || kForbiddenCharacters.contains(character)) {
        return SubfolderError::InvalidCharacter;
      }
    }
    parts.append(part);
  }

  normalized = parts.join(QLatin1Char('/'));
  return SubfolderError::None;
}

QString ScreenshotLocation::storedSubfolder(const QString &normalized)
{
  return normalized.isEmpty() ? QStringLiteral(".") : normalized;
}

bool ScreenshotLocation::sendsChanges(const SyncSummary::Folder &folder)
{
  if (folder.sharedWith.isEmpty()) {
    return false;
  }
  // a receive-only folder keeps local additions to itself, an encrypted one is never written to locally
  return folder.type != QStringLiteral("receiveonly") && folder.type != QStringLiteral("receiveencrypted");
}

QList<SyncSummary::Folder> ScreenshotLocation::eligibleFolders(const SyncSummary &summary)
{
  QList<SyncSummary::Folder> folders;
  for (const auto &folder : summary.folders) {
    if (sendsChanges(folder) && !folder.path.isEmpty()) {
      folders.append(folder);
    }
  }
  return folders;
}

QStringList ScreenshotLocation::peerNamesOf(const SyncSummary &summary, const SyncSummary::Folder &folder)
{
  QStringList names;
  for (const auto &deviceId : folder.sharedWith) {
    QString name;
    for (const auto &peer : summary.peers) {
      if (peer.id == deviceId) {
        name = peer.name;
        break;
      }
    }
    names.append(name.isEmpty() ? deviceId.left(kShortDeviceIdLength) : name);
  }
  return names;
}

QString ScreenshotLocation::joinPath(const QString &root, const QString &subfolder)
{
  if (root.isEmpty()) {
    return {};
  }
  if (subfolder.isEmpty()) {
    return QDir::cleanPath(root);
  }
  return QDir::cleanPath(root + QLatin1Char('/') + subfolder);
}

ScreenshotLocation ScreenshotLocation::resolve(
    const SyncSummary &summary, const QString &folderId, const QString &subfolder, const QString &fallbackRoot
)
{
  ScreenshotLocation location;
  location.folderId = folderId;
  if (normalizeSubfolder(subfolder, location.subfolder) != SubfolderError::None) {
    // a hand-edited setting that would leave the folder: use the default instead
    location.subfolder = QString::fromLatin1(kScreenshotSubfolder);
  }

  if (summary.apiReachable) {
    for (const auto &folder : summary.folders) {
      if (folder.id != folderId || folder.path.isEmpty()) {
        continue;
      }
      location.status = sendsChanges(folder) ? Status::Synced : Status::NotShared;
      location.folderLabel = folder.label.isEmpty() ? folder.id : folder.label;
      location.folderPath = folder.path;
      location.peerNames = peerNamesOf(summary, folder);
      location.directory = joinPath(folder.path, location.subfolder);
      return location;
    }
    location.status = Status::FolderMissing;
  } else {
    location.status = Status::SyncUnavailable;
  }

  // the folder cannot be confirmed: never lose a screenshot, keep it where the folder was last seen
  location.folderPath = fallbackRoot.isEmpty() ? QString() : QDir::cleanPath(fallbackRoot);
  location.directory = joinPath(location.folderPath, location.subfolder);
  return location;
}

} // namespace deskflow::gui
