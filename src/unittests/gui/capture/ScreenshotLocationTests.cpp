/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenshotLocationTests.h"

#include "gui/capture/ScreenshotLocation.h"
#include "gui/sync/SyncthingMonitor.h"

#include <QDir>

using deskflow::gui::ScreenshotLocation;
using deskflow::gui::sync::SyncSummary;
using deskflow::gui::sync::SyncthingMonitor;
using Status = ScreenshotLocation::Status;
using SubfolderError = ScreenshotLocation::SubfolderError;

namespace {

SyncSummary::Folder folder(const QString &id, const QString &path, const QString &type, const QStringList &sharedWith)
{
  SyncSummary::Folder entry;
  entry.id = id;
  entry.label = QStringLiteral("Label %1").arg(id);
  entry.path = path;
  entry.type = type;
  entry.sharedWith = sharedWith;
  entry.shared = !sharedWith.isEmpty();
  return entry;
}

SyncSummary summary()
{
  SyncSummary result;
  result.apiReachable = true;
  result.myId = QStringLiteral("ME");
  SyncSummary::Peer peer;
  peer.id = QStringLiteral("PEERDEVICEID");
  peer.name = QStringLiteral("laptop");
  result.peers.append(peer);
  result.folders.append(folder(
      QStringLiteral("deskflow-sync"), QStringLiteral("/home/u/NewSync"), QStringLiteral("sendreceive"),
      {QStringLiteral("PEERDEVICEID")}
  ));
  result.folders.append(
      folder(QStringLiteral("local"), QStringLiteral("/home/u/Local"), QStringLiteral("sendreceive"), {})
  );
  result.folders.append(folder(
      QStringLiteral("inbox"), QStringLiteral("/home/u/Inbox"), QStringLiteral("receiveonly"),
      {QStringLiteral("PEERDEVICEID")}
  ));
  result.folders.append(folder(
      QStringLiteral("photos"), QStringLiteral("/home/u/Photos"), QStringLiteral("sendonly"),
      {QStringLiteral("OTHERDEVICEID")}
  ));
  return result;
}

} // namespace

void ScreenshotLocationTests::normalizeSubfolder_data()
{
  QTest::addColumn<QString>("input");
  QTest::addColumn<int>("error");
  QTest::addColumn<QString>("normalized");

  const auto none = static_cast<int>(SubfolderError::None);
  QTest::newRow("default") << "screenshots" << none << "screenshots";
  QTest::newRow("empty is the folder itself") << "" << none << "";
  QTest::newRow("dot is the folder itself") << "." << none << "";
  QTest::newRow("spaces trimmed") << "  shots  " << none << "shots";
  QTest::newRow("nested") << "notes/screenshots" << none << "notes/screenshots";
  QTest::newRow("backslashes and dots") << "a\\b/./c/" << none << "a/b/c";
  QTest::newRow("duplicate separators") << "a//b" << none << "a/b";
  QTest::newRow("unicode") << QStringLiteral("截图/课堂") << none << QStringLiteral("截图/课堂");
  QTest::newRow("tilde name is fine") << "~old" << none << "~old";

  const auto absolute = static_cast<int>(SubfolderError::Absolute);
  QTest::newRow("absolute") << "/tmp/x" << absolute << "";
  QTest::newRow("home") << "~/x" << absolute << "";
  QTest::newRow("home alone") << "~" << absolute << "";
  QTest::newRow("drive letter") << "C:\\shots" << absolute << "";
  QTest::newRow("windows root") << "\\shots" << absolute << "";
  QTest::newRow("drive relative") << "a:b" << absolute << "";

  const auto parent = static_cast<int>(SubfolderError::ParentReference);
  QTest::newRow("parent") << "../x" << parent << "";
  QTest::newRow("parent inside") << "a/../../x" << parent << "";

  const auto invalid = static_cast<int>(SubfolderError::InvalidCharacter);
  QTest::newRow("question mark") << "what?" << invalid << "";
  QTest::newRow("colon inside") << "notes:old" << invalid << "";
  QTest::newRow("control character") << QStringLiteral("a\tb") << invalid << "";
}

void ScreenshotLocationTests::normalizeSubfolder()
{
  QFETCH(QString, input);
  QFETCH(int, error);
  QFETCH(QString, normalized);

  QString result;
  QCOMPARE(static_cast<int>(ScreenshotLocation::normalizeSubfolder(input, result)), error);
  if (error == static_cast<int>(SubfolderError::None)) {
    QCOMPARE(result, normalized);
  }
}

void ScreenshotLocationTests::storedSubfolder()
{
  QCOMPARE(ScreenshotLocation::storedSubfolder(QString()), QStringLiteral("."));
  QCOMPARE(ScreenshotLocation::storedSubfolder(QStringLiteral("a/b")), QStringLiteral("a/b"));

  // what is stored reads back as the same location
  QString normalized;
  QCOMPARE(ScreenshotLocation::normalizeSubfolder(QStringLiteral("."), normalized), SubfolderError::None);
  QVERIFY(normalized.isEmpty());
}

void ScreenshotLocationTests::expandPath()
{
  const auto home = QDir::homePath();
  QCOMPARE(SyncthingMonitor::expandPath(QStringLiteral("~")), home);
  QCOMPARE(SyncthingMonitor::expandPath(QStringLiteral("~/Sync")), home + QStringLiteral("/Sync"));
  QCOMPARE(SyncthingMonitor::expandPath(QStringLiteral("~/a/../Sync/")), home + QStringLiteral("/Sync"));
  QCOMPARE(SyncthingMonitor::expandPath(QStringLiteral("/data/Sync")), QStringLiteral("/data/Sync"));
  // like Syncthing, only "~" followed by a separator is the home directory
  QCOMPARE(SyncthingMonitor::expandPath(QStringLiteral("~other/Sync")), QStringLiteral("~other/Sync"));
  QCOMPARE(SyncthingMonitor::expandPath(QString()), QString());
}

void ScreenshotLocationTests::resolveSyncedFolder()
{
  const auto location = ScreenshotLocation::resolve(
      summary(), QStringLiteral("deskflow-sync"), QStringLiteral("screenshots"), QStringLiteral("/home/u/OldSync")
  );
  QCOMPARE(location.status, Status::Synced);
  QVERIFY(location.isSynced());
  QVERIFY(location.folderFound());
  // the live path from Syncthing wins over the cached one
  QCOMPARE(location.folderPath, QStringLiteral("/home/u/NewSync"));
  QCOMPARE(location.directory, QStringLiteral("/home/u/NewSync/screenshots"));
  QCOMPARE(location.folderLabel, QStringLiteral("Label deskflow-sync"));
  QCOMPARE(location.peerNames, QStringList{QStringLiteral("laptop")});
}

void ScreenshotLocationTests::resolveFolderRoot()
{
  const auto location =
      ScreenshotLocation::resolve(summary(), QStringLiteral("photos"), QStringLiteral("."), QString());
  QCOMPARE(location.status, Status::Synced);
  QCOMPARE(location.subfolder, QString());
  QCOMPARE(location.directory, QStringLiteral("/home/u/Photos"));
  // a device that is not a configured peer is shown by its short id
  QCOMPARE(location.peerNames, QStringList{QStringLiteral("OTHERDE")});
}

void ScreenshotLocationTests::resolveNotShared()
{
  const auto unshared =
      ScreenshotLocation::resolve(summary(), QStringLiteral("local"), QStringLiteral("shots"), QString());
  QCOMPARE(unshared.status, Status::NotShared);
  QVERIFY(unshared.folderFound());
  QCOMPARE(unshared.directory, QStringLiteral("/home/u/Local/shots"));

  const auto receiveOnly =
      ScreenshotLocation::resolve(summary(), QStringLiteral("inbox"), QStringLiteral("shots"), QString());
  QCOMPARE(receiveOnly.status, Status::NotShared);
  QCOMPARE(receiveOnly.directory, QStringLiteral("/home/u/Inbox/shots"));
}

void ScreenshotLocationTests::resolveMissingFolder()
{
  const auto location = ScreenshotLocation::resolve(
      summary(), QStringLiteral("removed"), QStringLiteral("screenshots"), QStringLiteral("/home/u/OldSync/")
  );
  QCOMPARE(location.status, Status::FolderMissing);
  QVERIFY(!location.folderFound());
  QVERIFY(location.folderLabel.isEmpty());
  // never lose a screenshot: it goes where the folder was last seen
  QCOMPARE(location.directory, QStringLiteral("/home/u/OldSync/screenshots"));

  const auto nowhere =
      ScreenshotLocation::resolve(summary(), QStringLiteral("removed"), QStringLiteral("screenshots"), QString());
  QVERIFY(nowhere.directory.isEmpty());
}

void ScreenshotLocationTests::resolveWhileSyncUnavailable()
{
  SyncSummary offline;
  const auto location = ScreenshotLocation::resolve(
      offline, QStringLiteral("deskflow-sync"), QStringLiteral("screenshots"), QStringLiteral("/home/u/DeskflowSync")
  );
  QCOMPARE(location.status, Status::SyncUnavailable);
  QCOMPARE(location.directory, QStringLiteral("/home/u/DeskflowSync/screenshots"));

  // a stale summary is not trusted once the API is unreachable
  auto stale = summary();
  stale.apiReachable = false;
  const auto staleLocation = ScreenshotLocation::resolve(
      stale, QStringLiteral("deskflow-sync"), QStringLiteral("screenshots"), QStringLiteral("/home/u/DeskflowSync")
  );
  QCOMPARE(staleLocation.status, Status::SyncUnavailable);
  QCOMPARE(staleLocation.directory, QStringLiteral("/home/u/DeskflowSync/screenshots"));
}

void ScreenshotLocationTests::resolveInvalidStoredSubfolder()
{
  const auto location =
      ScreenshotLocation::resolve(summary(), QStringLiteral("deskflow-sync"), QStringLiteral("../../etc"), QString());
  QCOMPARE(location.subfolder, QStringLiteral("screenshots"));
  QCOMPARE(location.directory, QStringLiteral("/home/u/NewSync/screenshots"));
}

void ScreenshotLocationTests::eligibleFolders()
{
  QStringList ids;
  for (const auto &entry : ScreenshotLocation::eligibleFolders(summary())) {
    ids.append(entry.id);
  }
  QCOMPARE(ids, (QStringList{QStringLiteral("deskflow-sync"), QStringLiteral("photos")}));

  auto encrypted =
      folder(QStringLiteral("vault"), QStringLiteral("/v"), QStringLiteral("receiveencrypted"), {QStringLiteral("X")});
  QVERIFY(!ScreenshotLocation::sendsChanges(encrypted));
  // an unknown type is treated like the default send and receive folder
  auto untyped = folder(QStringLiteral("t"), QStringLiteral("/t"), QString(), {QStringLiteral("X")});
  QVERIFY(ScreenshotLocation::sendsChanges(untyped));
}

QTEST_MAIN(ScreenshotLocationTests)
