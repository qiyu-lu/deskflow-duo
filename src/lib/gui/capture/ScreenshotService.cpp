/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ScreenshotService.h"

#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QPixmap>
#include <QScreen>
#include <QSysInfo>

#ifdef Q_OS_LINUX
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QEventLoop>
#include <QFile>
#include <QTimer>
#include <QUrl>
#endif


namespace deskflow::gui {

ScreenshotService::ScreenshotService(QObject *parent) : QObject(parent)
{
  // do nothing
}

bool ScreenshotService::isWayland()
{
  return QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive);
}

QString ScreenshotService::buildOutputPath(const QString &directory) const
{
  QDir dir(directory);
  if (!dir.exists() && !dir.mkpath(QStringLiteral("."))) {
    return {};
  }

  const QString host = QSysInfo::machineHostName();
  const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmsszzz"));
  const QString name = QStringLiteral("%1-%2.png").arg(host.isEmpty() ? QStringLiteral("screen") : host, stamp);
  return dir.filePath(name);
}

QString ScreenshotService::notSyncedReason(const ScreenshotLocation &location, bool syncEnabled)
{
  using Status = ScreenshotLocation::Status;
  switch (location.status) {
  case Status::Synced:
    return {};
  case Status::NotShared:
    return tr("the folder is not shared with another computer, or this computer only receives changes into it");
  case Status::FolderMissing:
    return tr("file sync no longer has the chosen folder");
  case Status::SyncUnavailable:
    return syncEnabled ? tr("file sync is not running") : tr("file sync is turned off");
  }
  return {};
}

bool ScreenshotService::saveWithQtGrab(const QString &filePath, QString &errorOut) const
{
  QScreen *screen = QGuiApplication::primaryScreen();
  if (screen == nullptr) {
    errorOut = tr("no primary screen available");
    return false;
  }

  // grabWindow(0) captures the whole of this screen with no prompt on X11/Windows.
  const QPixmap pixmap = screen->grabWindow(0);
  if (pixmap.isNull()) {
    errorOut = tr("screen grab returned an empty image");
    return false;
  }

  if (!pixmap.save(filePath, "PNG")) {
    errorOut = tr("could not write the image to %1").arg(filePath);
    return false;
  }

  return true;
}

QString ScreenshotService::capture(const QString &directory)
{
  if (directory.isEmpty()) {
    const QString message = tr("file sync folder is not configured yet");
    qWarning("screenshot capture skipped: %s", qPrintable(message));
    Q_EMIT failed(message);
    return {};
  }

  const QString filePath = buildOutputPath(directory);
  if (filePath.isEmpty()) {
    const QString message = tr("could not create the folder %1").arg(QDir::toNativeSeparators(directory));
    qWarning("screenshot capture failed: %s", qPrintable(message));
    Q_EMIT failed(message);
    return {};
  }

  QString error;
  const bool ok = isWayland() ? saveOnWayland(filePath, error) : saveWithQtGrab(filePath, error);
  if (!ok) {

    qWarning("screenshot capture failed: %s", qPrintable(error));
    Q_EMIT failed(error);
    return {};
  }

  qInfo("screenshot saved to %s", qPrintable(filePath));
  Q_EMIT captured(filePath);
  return filePath;
}

bool ScreenshotService::saveOnWayland(const QString &filePath, QString &errorOut)
{
#ifdef Q_OS_LINUX
  QDBusConnection bus = QDBusConnection::sessionBus();
  if (!bus.isConnected()) {
    errorOut = tr("session D-Bus is not available");
    return false;
  }

  QDBusInterface portal(
      QStringLiteral("org.freedesktop.portal.Desktop"), QStringLiteral("/org/freedesktop/portal/desktop"),
      QStringLiteral("org.freedesktop.portal.Screenshot"), bus
  );
  if (!portal.isValid()) {
    errorOut = tr("the screenshot portal is not available");
    return false;
  }

  QVariantMap options;
  // interactive=false asks the portal to capture without any picker UI. On
  // GNOME this is silent; KDE may show a one-time permission it then remembers.
  options.insert(QStringLiteral("interactive"), false);
  options.insert(QStringLiteral("handle_token"), QStringLiteral("deskflow_%1").arg(QDateTime::currentMSecsSinceEpoch()));

  QDBusReply<QDBusObjectPath> reply = portal.call(QStringLiteral("Screenshot"), QString(), options);
  if (!reply.isValid()) {
    errorOut = reply.error().message();
    return false;
  }
  const QString requestPath = reply.value().path();

  m_portalResponse = 1;
  m_portalUri.clear();
  QEventLoop loop;
  m_portalLoop = &loop;

  const bool connected = bus.connect(
      QStringLiteral("org.freedesktop.portal.Desktop"), requestPath, QStringLiteral("org.freedesktop.portal.Request"),
      QStringLiteral("Response"), this, SLOT(onPortalResponse(uint, QVariantMap))
  );
  if (!connected) {
    m_portalLoop = nullptr;
    errorOut = tr("could not listen for the screenshot portal response");
    return false;
  }

  // Never block the GUI indefinitely if the portal never answers.
  QTimer::singleShot(10000, &loop, &QEventLoop::quit);
  loop.exec();

  bus.disconnect(
      QStringLiteral("org.freedesktop.portal.Desktop"), requestPath, QStringLiteral("org.freedesktop.portal.Request"),
      QStringLiteral("Response"), this, SLOT(onPortalResponse(uint, QVariantMap))
  );
  m_portalLoop = nullptr;

  if (m_portalResponse != 0 || m_portalUri.isEmpty()) {
    errorOut = tr("the screenshot portal declined the request or timed out");
    return false;
  }

  const QString tempFile = QUrl(m_portalUri).toLocalFile();
  if (tempFile.isEmpty()) {
    errorOut = tr("the screenshot portal returned an unusable location");
    return false;
  }

  QFile::remove(filePath);
  if (!QFile::copy(tempFile, filePath)) {
    errorOut = tr("could not copy the portal screenshot to %1").arg(filePath);
    return false;
  }
  QFile::remove(tempFile);
  return true;
#else
  Q_UNUSED(filePath)
  errorOut = tr("Wayland capture is only available on Linux");
  return false;
#endif
}

void ScreenshotService::onPortalResponse(uint response, const QVariantMap &results)
{
  m_portalResponse = response;
  const auto it = results.constFind(QStringLiteral("uri"));
  if (it != results.constEnd()) {
    m_portalUri = it.value().toString();
  }
  if (m_portalLoop != nullptr) {
    m_portalLoop->quit();
  }
}

} // namespace deskflow::gui

