/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>
#include <QUrl>
#include <QUrlQuery>

#include <functional>

class QNetworkReply;

namespace deskflow::gui::sync {

/**
 * @brief Thin asynchronous wrapper around the Syncthing REST API.
 *
 * Only the handful of endpoints the GUI needs are exposed. Every call takes a callback that
 * receives a Reply; callbacks are invoked on the client's thread, never synchronously.
 */
class SyncthingClient : public QObject
{
  Q_OBJECT

public:
  struct Reply
  {
    bool ok = false;   //!< transport succeeded and the HTTP status is 2xx
    int status = 0;    //!< HTTP status, 0 on transport failure
    QString error;     //!< human readable error when !ok
    QByteArray body;

    QJsonDocument json() const
    {
      return QJsonDocument::fromJson(body);
    }
    QJsonObject object() const
    {
      return json().object();
    }
    QJsonArray array() const
    {
      return json().array();
    }
  };
  using Callback = std::function<void(const Reply &)>;

  explicit SyncthingClient(QObject *parent = nullptr);
  ~SyncthingClient() override;

  void setEndpoint(const QUrl &baseUrl, const QByteArray &apiKey);
  void clearEndpoint();
  bool hasEndpoint() const
  {
    return !m_baseUrl.isEmpty();
  }
  QUrl baseUrl() const
  {
    return m_baseUrl;
  }
  void abortAll();

  // generic
  void get(const QString &path, const QUrlQuery &query, Callback callback);
  void post(const QString &path, const QUrlQuery &query, const QJsonValue &body, Callback callback);
  void put(const QString &path, const QUrlQuery &query, const QJsonValue &body, Callback callback);
  void patch(const QString &path, const QUrlQuery &query, const QJsonValue &body, Callback callback);
  void del(const QString &path, const QUrlQuery &query, Callback callback);

  // status
  void ping(Callback callback);
  void systemStatus(Callback callback);
  void connections(Callback callback);
  void config(Callback callback);
  void dbStatus(const QString &folderId, Callback callback);
  void completion(const QString &deviceId, const QString &folderId, Callback callback);
  void systemErrors(Callback callback);
  void discovery(Callback callback);
  void pendingDevices(Callback callback);

  // changes
  void setDevice(const QJsonObject &device, Callback callback);
  void setFolder(const QJsonObject &folder, Callback callback);
  void getFolder(const QString &folderId, Callback callback);
  void patchFolder(const QString &folderId, const QJsonObject &changes, Callback callback);
  void setDefaultFolder(const QJsonObject &changes, Callback callback);
  void pauseAll(bool paused, Callback callback);
  void dismissPendingDevice(const QString &deviceId, Callback callback);
  void shutdown(Callback callback);

private:
  void send(const QByteArray &verb, const QString &path, const QUrlQuery &query, const QByteArray &body, Callback callback);

  QNetworkAccessManager m_network;
  QUrl m_baseUrl;
  QByteArray m_apiKey;
  QSet<QNetworkReply *> m_pending;
};

} // namespace deskflow::gui::sync
