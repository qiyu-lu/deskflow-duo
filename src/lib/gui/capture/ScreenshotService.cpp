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

namespace deskflow::gui {

ScreenshotService::ScreenshotService(QObject *parent) : QObject(parent)
{
  // do nothing
}

bool ScreenshotService::isWayland()
{
  return QGuiApplication::platformName().startsWith(QLatin1String("wayland"), Qt::CaseInsensitive);
}

QString ScreenshotService::buildOutputPath(const QString &folderPath) const
{
  QDir dir(folderPath);
  const QString subDir = QStringLiteral("screenshots");
  if (!dir.exists(subDir) && !dir.mkpath(subDir)) {
    return {};
  }

  const QString host = QSysInfo::machineHostName();
  const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmsszzz"));
  const QString name = QStringLiteral("%1-%2.png").arg(host.isEmpty() ? QStringLiteral("screen") : host, stamp);
  return dir.filePath(QStringLiteral("%1/%2").arg(subDir, name));
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

QString ScreenshotService::capture(const QString &folderPath)
{
  if (folderPath.isEmpty()) {
    const QString message = tr("file sync folder is not configured yet");
    qWarning("screenshot capture skipped: %s", qPrintable(message));
    Q_EMIT failed(message);
    return {};
  }

  const QString filePath = buildOutputPath(folderPath);
  if (filePath.isEmpty()) {
    const QString message = tr("could not create the screenshots folder under %1").arg(folderPath);
    qWarning("screenshot capture failed: %s", qPrintable(message));
    Q_EMIT failed(message);
    return {};
  }

  if (isWayland()) {
    // Wayland blocks silent framebuffer grabs by design; a compositor-specific
    // path is added separately. Fail clearly rather than writing a black image.
    const QString message = tr("silent capture on Wayland is not supported yet");
    qWarning("screenshot capture skipped: %s", qPrintable(message));
    Q_EMIT failed(message);
    return {};
  }

  QString error;
  if (!saveWithQtGrab(filePath, error)) {
    qWarning("screenshot capture failed: %s", qPrintable(error));
    Q_EMIT failed(error);
    return {};
  }

  qInfo("screenshot saved to %s", qPrintable(filePath));
  Q_EMIT captured(filePath);
  return filePath;
}

} // namespace deskflow::gui
