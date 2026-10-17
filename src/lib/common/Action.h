/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2008 Volker Lanz <vl@fidra.de>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/KeySequence.h"

#include <QList>
#include <QString>
#include <QStringList>

class QSettings;
class QTextStream;

struct SettingsKeys
{
  inline static const QString ActionType = QStringLiteral("type");
  inline static const QString ScreenNames = QStringLiteral("typeScreenNames");
  inline static const QString ScreenName = QStringLiteral("typeScreenName");
  inline static const QString SwitchToScreen = QStringLiteral("switchScreenName");
  inline static const QString SwitchDirection = QStringLiteral("switchInDirection");
  inline static const QString LockToScreen = QStringLiteral("lockCursorToScreen");
  inline static const QString MouseBroadcast = QStringLiteral("mouseBroadcast");
  inline static const QString KeyboardBroadcast = QStringLiteral("keyboardBroadcast");
  inline static const QString ActiveOnRelease = QStringLiteral("activeOnRelease");
  inline static const QString HasScreens = QStringLiteral("hasScreens");
  inline static const QString RestartServer = QStringLiteral("restartServer");
};

class Action
{

public:
  enum class Type
  {
    keyDown,
    keyUp,
    keystroke,
    switchToScreen,
    switchInDirection,
    switchToNextScreen,
    lockCursorToScreen,
    restartAllConnections,
    mouseDown,
    mouseUp,
    mousebutton,
    mouseBroadcast,
    keyboardBroadcast,
    screenshot,
  };
  enum class SwitchDirection

  {
    left,
    right,
    up,
    down
  };
  enum class LockCursorMode
  {
    toggle,
    on,
    off
  };
  enum class MouseBroadcastMode
  {
    toggle,
    on,
    off
  };

public:
  Action() = default;

  QString text() const;

  void loadSettings(QSettings &settings);
  void saveSettings(QSettings &settings) const;

  const KeySequence &keySequence() const;
  void setKeySequence(const KeySequence &seq);

  int type() const;
  void setType(int t);

  QStringList typeScreenNames() const;
  void clearScreens();
  void addScreen(const QString &screen);
  void removeScreen(const QString &screen);

  const QString &switchScreenName() const;
  void setSwitchScreenName(const QString &n);

  int switchDirection() const;
  void setSwitchDirection(int d);

  int lockCursorMode() const;
  void setLockCursorMode(int m);

  int mouseBroadcastMode() const;
  void setMouseBroadcastMode(int m);

  int keyboardBroadcastMode() const;
  void setKeyboardBroadcastMode(int m);

  bool activeOnRelease() const;
  void setActiveOnRelease(bool b);

  bool haveScreens() const;
  void setHaveScreens(bool b);

  bool restartServer() const;
  void setRestartServer(bool b);

  bool operator==(const Action &a) const = default;

private:
  KeySequence m_keySequence;
  int m_type = static_cast<int>(Type::keystroke);
  QStringList m_typeScreenNames = QStringList();
  QString m_switchScreenName = QString();
  int m_switchDirection = static_cast<int>(SwitchDirection::left);
  int m_lockCursorMode = static_cast<int>(LockCursorMode::toggle);
  int m_mouseBroadcastMode = static_cast<int>(MouseBroadcastMode::toggle);
  int m_keyboardBroadcastMode = static_cast<int>(MouseBroadcastMode::toggle);
  bool m_activeOnRelease = false;
  bool m_hasScreens = false;
  bool m_restartServer;

  inline static const QString m_commandTemplate = QStringLiteral("(%1)");
  inline static const QStringList m_actionTypeNames{
      QStringLiteral("keyDown"),
      QStringLiteral("keyUp"),
      QStringLiteral("keystroke"),
      QStringLiteral("switchToScreen"),
      QStringLiteral("switchInDirection"),
      QStringLiteral("switchToNextScreen"),
      QStringLiteral("lockCursorToScreen"),
      QStringLiteral("restartServer"),
      QStringLiteral("mouseDown"),
      QStringLiteral("mouseUp"),
      QStringLiteral("mousebutton"),
      QStringLiteral("mouseBroadcast"),
      QStringLiteral("keyboardBroadcast"),
      QStringLiteral("screenshot")
  };


  inline static const QStringList m_switchDirectionNames{
      QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("up"), QStringLiteral("down")
  };

  inline static const QStringList m_lockCursorModeNames{
      QStringLiteral("toggle"), QStringLiteral("on"), QStringLiteral("off")
  };

  inline static const QStringList m_mouseBroadcastModeNames{
      QStringLiteral("toggle"), QStringLiteral("on"), QStringLiteral("off")
  };
};

using ActionList = QList<Action>;

QTextStream &operator<<(QTextStream &outStream, const Action &action);
