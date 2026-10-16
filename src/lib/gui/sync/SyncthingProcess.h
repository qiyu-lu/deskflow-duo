/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QObject>
#include <QProcess>
#include <QTimer>
#include <QUrl>

namespace deskflow::gui::sync {

/**
 * @brief Runs the bundled Syncthing binary as a child of the GUI.
 *
 * The instance uses its own home directory below the Deskflow settings path so it never
 * interferes with a Syncthing installation the user may already have. The REST API is bound to
 * the loopback interface on a free port and protected with a per-session API key, both of which
 * are handed to the GUI through apiEndpointChanged().
 */
class SyncthingProcess : public QObject
{
  Q_OBJECT

public:
  enum class State
  {
    Unavailable, //!< no usable syncthing binary was found
    Stopped,
    Starting,
    Running,
    Stopping,
    Failed //!< crashed repeatedly, not restarting anymore
  };
  Q_ENUM(State)

  explicit SyncthingProcess(QObject *parent = nullptr);
  ~SyncthingProcess() override;

  /**
   * @brief Find the syncthing binary to run.
   * @return absolute path, or an empty string when nothing usable was found.
   */
  static QString locateBinary();

  /** @brief Directory holding the config, keys, database and log of the managed instance. */
  static QString homeDir();

  void start();
  void stop();

  State state() const
  {
    return m_state;
  }
  bool isRunning() const
  {
    return m_state == State::Running;
  }
  QUrl apiUrl() const
  {
    return m_apiUrl;
  }
  QByteArray apiKey() const
  {
    return m_apiKey;
  }
  QString binaryPath() const
  {
    return m_binaryPath;
  }
  QString logFile() const;

Q_SIGNALS:
  void stateChanged(deskflow::gui::sync::SyncthingProcess::State state);
  void apiEndpointChanged(const QUrl &url, const QByteArray &apiKey);
  void errorOccurred(const QString &message);

private:
  void setState(State state);
  bool ensureConfig();
  void launch();
  void onStarted();
  void onFinished(int exitCode, QProcess::ExitStatus status);
  void onProcessError(QProcess::ProcessError error);
  void readOutput();
  static quint16 pickFreePort(quint16 preferred);
  static QByteArray makeApiKey();

  QProcess *m_process = nullptr;
  QTimer m_retryTimer;
  QTimer m_killTimer;
  State m_state = State::Stopped;
  QString m_binaryPath;
  QUrl m_apiUrl;
  QByteArray m_apiKey;
  int m_crashCount = 0;
  bool m_stopRequested = false;
};

} // namespace deskflow::gui::sync
