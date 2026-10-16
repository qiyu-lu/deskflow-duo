/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "PairingServiceTests.h"

#include "gui/pairing/PairingService.h"

#include <QRegularExpression>

using deskflow::gui::pairing::PairingInfo;
using deskflow::gui::pairing::PairingService;

namespace {

struct Pair
{
  PairingService server;
  PairingService client;
  PairingInfo serverInfo;
  PairingInfo clientInfo;
  int serverExchanged = 0;
  int clientExchanged = 0;
  int serverFailed = 0;
  int clientFailed = 0;
  QString serverCode;
  QString clientCode;
  QString serverError;
  QString clientError;
  QList<bool> serverFinished;
  QList<bool> clientFinished;

  Pair()
  {
    serverInfo.computerName = QStringLiteral("desk");
    serverInfo.isServer = true;
    serverInfo.clientOnLeft = true;
    serverInfo.syncDeviceId = QStringLiteral("SERVER-DEVICE-ID");
    serverInfo.tlsFingerprint = QStringLiteral("aa").repeated(32);

    clientInfo.computerName = QStringLiteral("laptop");
    clientInfo.isServer = false;
    clientInfo.syncDeviceId = QStringLiteral("CLIENT-DEVICE-ID");
    clientInfo.tlsFingerprint = QStringLiteral("bb").repeated(32);

    QObject::connect(&server, &PairingService::exchanged, [this](const PairingInfo &, const QString &code) {
      serverExchanged++;
      serverCode = code;
    });
    QObject::connect(&client, &PairingService::exchanged, [this](const PairingInfo &, const QString &code) {
      clientExchanged++;
      clientCode = code;
    });
    QObject::connect(&server, &PairingService::failed, [this](const QString &error) {
      serverFailed++;
      serverError = error;
    });
    QObject::connect(&client, &PairingService::failed, [this](const QString &error) {
      clientFailed++;
      clientError = error;
    });
    QObject::connect(&server, &PairingService::finished, [this](bool accepted) { serverFinished.append(accepted); });
    QObject::connect(&client, &PairingService::finished, [this](bool accepted) { clientFinished.append(accepted); });
  }

  void start()
  {
    server.setLocalInfo(serverInfo);
    client.setLocalInfo(clientInfo);
    QVERIFY(server.listen(0));
    QVERIFY(server.listeningPort() != 0);
    client.connectToServer(QStringLiteral("127.0.0.1"), server.listeningPort());
  }
};

} // namespace

void PairingServiceTests::testConfirmationCodeFormat()
{
  Pair pair;
  const auto code = PairingService::makeConfirmationCode(pair.serverInfo, pair.clientInfo);
  QCOMPARE(code.size(), 6);
  QVERIFY(QRegularExpression(QStringLiteral("^[0-9]{6}$")).match(code).hasMatch());
  QCOMPARE(PairingService::makeConfirmationCode(pair.serverInfo, pair.clientInfo), code);

  auto other = pair.clientInfo;
  other.tlsFingerprint = QStringLiteral("cc").repeated(32);
  QVERIFY(PairingService::makeConfirmationCode(pair.serverInfo, other) != code);
}

void PairingServiceTests::testExchangeAndAccept()
{
  Pair pair;
  pair.start();

  QTRY_COMPARE(pair.serverExchanged, 1);
  QTRY_COMPARE(pair.clientExchanged, 1);
  QCOMPARE(pair.serverCode, pair.clientCode);
  QCOMPARE(pair.serverCode, PairingService::makeConfirmationCode(pair.serverInfo, pair.clientInfo));
  QCOMPARE(pair.server.remoteInfo().computerName, QStringLiteral("laptop"));
  QCOMPARE(pair.server.remoteInfo().syncDeviceId, QStringLiteral("CLIENT-DEVICE-ID"));
  QCOMPARE(pair.client.remoteInfo().computerName, QStringLiteral("desk"));
  QVERIFY(pair.client.remoteInfo().isServer);
  QVERIFY(pair.client.remoteInfo().clientOnLeft);
  QCOMPARE(pair.server.phase(), PairingService::Phase::Exchanged);
  QCOMPARE(pair.client.phase(), PairingService::Phase::Exchanged);
  QVERIFY(!pair.server.remoteAddress().isEmpty());

  pair.server.confirm(true);
  QTRY_COMPARE(pair.serverFinished, QList<bool>{true});
  QTRY_COMPARE(pair.clientFinished, QList<bool>{true});
  QCOMPARE(pair.server.phase(), PairingService::Phase::Accepted);
  QCOMPARE(pair.client.phase(), PairingService::Phase::Accepted);
  QCOMPARE(pair.serverFailed, 0);
  QCOMPARE(pair.clientFailed, 0);
}

void PairingServiceTests::testExchangeAndReject()
{
  Pair pair;
  pair.start();
  QTRY_COMPARE(pair.clientExchanged, 1);
  QTRY_COMPARE(pair.serverExchanged, 1);

  pair.server.confirm(false);
  QTRY_COMPARE(pair.serverFinished, QList<bool>{false});
  QTRY_COMPARE(pair.clientFinished, QList<bool>{false});
  QCOMPARE(pair.client.phase(), PairingService::Phase::Rejected);
}

void PairingServiceTests::testSameRoleFails()
{
  Pair pair;
  pair.clientInfo.isServer = true;
  pair.start();

  QTRY_COMPARE(pair.serverFailed, 1);
  QTRY_COMPARE(pair.clientFailed, 1);
  QCOMPARE(pair.serverExchanged, 0);
  QCOMPARE(pair.clientExchanged, 0);
  QVERIFY(!pair.serverError.isEmpty());
  QVERIFY(!pair.clientError.isEmpty());
}

void PairingServiceTests::testSameNameFails()
{
  Pair pair;
  pair.clientInfo.computerName = QStringLiteral("DESK");
  pair.start();

  QTRY_COMPARE(pair.serverFailed, 1);
  QTRY_COMPARE(pair.clientFailed, 1);
  QCOMPARE(pair.serverExchanged, 0);
}

QTEST_MAIN(PairingServiceTests)
