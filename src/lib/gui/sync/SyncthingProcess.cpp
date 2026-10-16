/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SyncthingProcess.h"

#include "common/Constants.h"
#include "common/Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTcpServer>

#ifdef Q_OS_LINUX
#include <csignal>
#include <sys/prctl.h>
#endif

namespace deskflow::gui::sync {

namespace {
const quint16 kPreferredApiPort = 8386;
const int kGenerateTimeoutMs = 60000;
const int kMaxCrashRestarts = 5;
const int kKillDelayMs = 5000;
const auto kConfigFile = QStringLiteral("config.xml");
} // namespace

SyncthingProcess::SyncthingProcess(QObject *parent) : QObject(parent), m_process{new QProcess(this)}
{
  m_process->setProcessChannelMode(QProcess::MergedChannels);
  connect(m_process, &QProcess::started, this, &SyncthingProcess::onStarted);
  connect(m_process, &QProcess::finished, this, &SyncthingProcess::onFinished);
  connect(m_process, &QProcess::errorOccurred, this, &SyncthingProcess::onProcessError);
  connect(m_process, &QProcess::readyReadStandardOutput, this, &SyncthingProcess::readOutput);

  m_retryTimer.setSingleShot(true);
  connect(&m_retryTimer, &QTimer::timeout, this, &SyncthingProcess::launch);

  m_killTimer.setSingleShot(true);
  connect(&m_killTimer, &QTimer::timeout, this, [this] {
    if (m_process->state() != QProcess::NotRunning) {
      qWarning("syncthing did not stop in time, killing it");
      m_process->kill();
    }
  });
}

SyncthingProcess::~SyncthingProcess()
{
  m_stopRequested = true;
  m_retryTimer.stop();
  disconnect(m_process, nullptr, this, nullptr);
  if (m_process->state() != QProcess::NotRunning) {
    m_process->terminate();
    if (!m_process->waitForFinished(3000)) {
      m_process->kill();
      m_process->waitForFinished(1000);
    }
  }
}

QString SyncthingProcess::locateBinary()
{
  const auto configured = Settings::value(Settings::Sync::BinaryPath).toString();
  if (!configured.isEmpty()) {
    if (const QFileInfo info(configured); info.isExecutable()) {
      return info.absoluteFilePath();
    }
    qWarning("configured syncthing binary is not executable: %s", qPrintable(configured));
  }

#ifdef Q_OS_WIN
  const auto exeName = QStringLiteral("%1.exe").arg(kSyncBinName);
#else
  const auto exeName = QString(kSyncBinName);
#endif

  const auto appDir = QCoreApplication::applicationDirPath();
  const QStringList candidates = {
      QStringLiteral("%1/%2").arg(appDir, exeName),
      QStringLiteral("%1/../lib/%2/%3").arg(appDir, kAppId, exeName),
      QStringLiteral("%1/../libexec/%2/%3").arg(appDir, kAppId, exeName),
  };
  for (const auto &candidate : candidates) {
    if (const QFileInfo info(candidate); info.isExecutable()) {
      return info.absoluteFilePath();
    }
  }

  return QStandardPaths::findExecutable(exeName);
}

QString SyncthingProcess::homeDir()
{
  return QStringLiteral("%1/%2").arg(Settings::settingsPath(), kSyncBinName);
}

QString SyncthingProcess::logFile() const
{
  return QStringLiteral("%1/%2.log").arg(homeDir(), kSyncBinName);
}

void SyncthingProcess::start()
{
  if (m_state == State::Starting || m_state == State::Running) {
    return;
  }

  m_stopRequested = false;
  m_crashCount = 0;
  m_binaryPath = locateBinary();
  if (m_binaryPath.isEmpty()) {
    setState(State::Unavailable);
    Q_EMIT errorOccurred(tr("The Syncthing program was not found. File sync is unavailable."));
    return;
  }

  if (!ensureConfig()) {
    setState(State::Failed);
    return;
  }

  launch();
}

void SyncthingProcess::stop()
{
  m_stopRequested = true;
  m_retryTimer.stop();

  if (m_process->state() == QProcess::NotRunning) {
    if (m_state != State::Unavailable) {
      setState(State::Stopped);
    }
    return;
  }

  setState(State::Stopping);
  // On Linux and macOS the monitor process forwards SIGTERM to the worker, which shuts down
  // cleanly. On Windows terminate() posts WM_CLOSE which a console program ignores, so the
  // owner asks the REST API to shut down first and this call is only the fallback.
  m_process->terminate();
  m_killTimer.start(kKillDelayMs);
}

void SyncthingProcess::setState(State state)
{
  if (m_state == state) {
    return;
  }
  m_state = state;
  Q_EMIT stateChanged(state);
}

bool SyncthingProcess::ensureConfig()
{
  const auto home = homeDir();
  if (!QDir().mkpath(home)) {
    Q_EMIT errorOccurred(tr("Could not create the Syncthing data directory: %1").arg(home));
    return false;
  }

  if (QFile::exists(QStringLiteral("%1/%2").arg(home, kConfigFile))) {
    return true;
  }

  qInfo("generating syncthing config in %s", qPrintable(home));
  QProcess generate;
  generate.setProgram(m_binaryPath);
  generate.setArguments({QStringLiteral("generate"), QStringLiteral("--home=%1").arg(home)});
  generate.setProcessChannelMode(QProcess::MergedChannels);
  generate.start();
  if (!generate.waitForFinished(kGenerateTimeoutMs) || generate.exitCode() != 0) {
    const auto output = QString::fromUtf8(generate.readAll()).trimmed();
    qCritical("syncthing generate failed: %s", qPrintable(output));
    Q_EMIT errorOccurred(tr("Could not create the Syncthing configuration."));
    return false;
  }
  return true;
}

void SyncthingProcess::launch()
{
  if (m_stopRequested || m_process->state() != QProcess::NotRunning) {
    return;
  }

  setState(State::Starting);

  const auto home = homeDir();
  const auto port = pickFreePort(kPreferredApiPort);
  m_apiKey = makeApiKey();
  m_apiUrl = QUrl(QStringLiteral("http://127.0.0.1:%1").arg(port));

  const QStringList args = {
      QStringLiteral("serve"),
      QStringLiteral("--home=%1").arg(home),
      QStringLiteral("--no-browser"),
      QStringLiteral("--no-restart"),
      QStringLiteral("--no-upgrade"),
      QStringLiteral("--gui-address=%1").arg(m_apiUrl.toString()),
      QStringLiteral("--gui-apikey=%1").arg(QString::fromLatin1(m_apiKey)),
      QStringLiteral("--log-file=%1").arg(logFile()),
      QStringLiteral("--log-max-size=2000000"),
      QStringLiteral("--log-max-old-files=2"),
  };

  auto env = QProcessEnvironment::systemEnvironment();
  for (const auto &key : {"STGUIADDRESS", "STGUIAPIKEY", "STHOMEDIR", "STGUIAUTH"}) {
    env.remove(QString::fromLatin1(key));
  }
  env.insert(QStringLiteral("STNOUPGRADE"), QStringLiteral("1"));
  env.insert(QStringLiteral("STNORESTART"), QStringLiteral("1"));
  env.insert(QStringLiteral("STNOBROWSER"), QStringLiteral("1"));
  m_process->setProcessEnvironment(env);

#ifdef Q_OS_LINUX
  m_process->setChildProcessModifier([] {
    // make sure syncthing goes away when the gui is killed abruptly
    prctl(PR_SET_PDEATHSIG, SIGTERM);
  });
#endif

  qInfo("starting syncthing: %s (api %s)", qPrintable(m_binaryPath), qPrintable(m_apiUrl.toString()));
  m_process->start(m_binaryPath, args);
}

void SyncthingProcess::onStarted()
{
  setState(State::Running);
  Q_EMIT apiEndpointChanged(m_apiUrl, m_apiKey);
}

void SyncthingProcess::onFinished(int exitCode, QProcess::ExitStatus status)
{
  m_killTimer.stop();
  readOutput();

  if (m_stopRequested) {
    qInfo("syncthing stopped");
    setState(State::Stopped);
    return;
  }

  m_crashCount++;
  qWarning("syncthing exited unexpectedly (code %d, %s)", exitCode, status == QProcess::CrashExit ? "crash" : "exit");

  if (m_crashCount > kMaxCrashRestarts) {
    setState(State::Failed);
    Q_EMIT errorOccurred(tr("Syncthing keeps exiting unexpectedly and will not be restarted. See the log: %1")
                             .arg(logFile()));
    return;
  }

  setState(State::Stopped);
  const int delayMs = qMin(30000, 1000 * (1 << m_crashCount));
  qInfo("restarting syncthing in %d ms", delayMs);
  m_retryTimer.start(delayMs);
}

void SyncthingProcess::onProcessError(QProcess::ProcessError error)
{
  if (error == QProcess::FailedToStart) {
    m_stopRequested = true;
    setState(State::Failed);
    Q_EMIT errorOccurred(tr("Could not start Syncthing: %1").arg(m_process->errorString()));
  }
}

void SyncthingProcess::readOutput()
{
  const auto text = QString::fromUtf8(m_process->readAllStandardOutput());
  const auto lines = text.split('\n', Qt::SkipEmptyParts);
  for (const auto &line : lines) {
    const auto trimmed = line.trimmed();
    if (trimmed.contains(QStringLiteral(" ERR ")) || trimmed.contains(QStringLiteral(" WRN "))) {
      qWarning("syncthing: %s", qPrintable(trimmed));
    } else {
      qDebug("syncthing: %s", qPrintable(trimmed));
    }
  }
}

quint16 SyncthingProcess::pickFreePort(quint16 preferred)
{
  QTcpServer probe;
  if (probe.listen(QHostAddress::LocalHost, preferred)) {
    probe.close();
    return preferred;
  }
  if (probe.listen(QHostAddress::LocalHost, 0)) {
    const auto port = probe.serverPort();
    probe.close();
    return port;
  }
  return preferred;
}

QByteArray SyncthingProcess::makeApiKey()
{
  static const auto alphabet = QByteArrayLiteral("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
  QByteArray key;
  key.reserve(32);
  auto *rng = QRandomGenerator::system();
  for (int i = 0; i < 32; i++) {
    key.append(alphabet.at(static_cast<int>(rng->bounded(static_cast<quint32>(alphabet.size())))));
  }
  return key;
}

} // namespace deskflow::gui::sync
