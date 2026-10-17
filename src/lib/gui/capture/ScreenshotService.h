/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class QEventLoop;


namespace deskflow::gui {

/**
 * @brief Captures the screen silently and drops the image into the synced folder.
 *
 * The capture is triggered by the "screenshot" core-IPC command, which the
 * server raises when its screenshot hotkey fires. No dialog or prompt is shown:
 * on X11 and Windows the grab is fully silent, matching tools such as Flameshot
 * (full-screen mode) and ShareX. Wayland needs a compositor-specific path and is
 * handled separately.
 */
class ScreenshotService : public QObject
{
  Q_OBJECT

public:
  explicit ScreenshotService(QObject *parent = nullptr);

  /**
   * @brief Capture the primary screen into <folderPath>/screenshots/.
   * @param folderPath the Syncthing-managed folder; capture is skipped if empty.
   * @return the saved file path, or an empty string on failure.
   */
  QString capture(const QString &folderPath);

Q_SIGNALS:
  void captured(const QString &filePath);
  void failed(const QString &message);

private Q_SLOTS:
  /// Receives the XDG screenshot portal's asynchronous Response signal.
  void onPortalResponse(uint response, const QVariantMap &results);

private:
  /// Build "<folderPath>/screenshots/<host>-<timestamp>.png", creating the dir.
  QString buildOutputPath(const QString &folderPath) const;

  /// Whether we are running under a Wayland session (grabWindow does not work there).
  static bool isWayland();

  /// Silent full-screen grab for X11/Windows/macOS. Empty image on failure.
  bool saveWithQtGrab(const QString &filePath, QString &errorOut) const;

  /// Capture via the XDG desktop portal (Wayland). Silent on GNOME with
  /// interactive=false; KDE may show a one-time permission prompt.
  bool saveOnWayland(const QString &filePath, QString &errorOut);

  // Transient state used only while a portal request is in flight. The capture
  // is driven from the GUI thread one request at a time, so plain members are safe.
  QEventLoop *m_portalLoop = nullptr;
  uint m_portalResponse = 1;
  QString m_portalUri;
};


} // namespace deskflow::gui
