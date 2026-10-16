/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "SyncthingClient.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace deskflow::gui::sync {

namespace {
const int kRequestTimeoutMs = 10000;
const auto kApiKeyHeader = QByteArrayLiteral("X-API-Key");

QByteArray encode(const QJsonValue &value)
{
  if (value.isObject()) {
    return QJsonDocument(value.toObject()).toJson(QJsonDocument::Compact);
  }
  if (value.isArray()) {
    return QJsonDocument(value.toArray()).toJson(QJsonDocument::Compact);
  }
  return {};
}
} // namespace

SyncthingClient::SyncthingClient(QObject *parent) : QObject(parent)
{
  m_network.setTransferTimeout(kRequestTimeoutMs);
}

SyncthingClient::~SyncthingClient()
{
  abortAll();
}

void SyncthingClient::setEndpoint(const QUrl &baseUrl, const QByteArray &apiKey)
{
  abortAll();
  m_baseUrl = baseUrl;
  m_apiKey = apiKey;
}

void SyncthingClient::clearEndpoint()
{
  abortAll();
  m_baseUrl.clear();
  m_apiKey.clear();
}

void SyncthingClient::abortAll()
{
  const auto pending = m_pending;
  m_pending.clear();
  for (auto *reply : pending) {
    disconnect(reply, nullptr, this, nullptr);
    reply->abort();
    reply->deleteLater();
  }
}

void SyncthingClient::send(
    const QByteArray &verb, const QString &path, const QUrlQuery &query, const QByteArray &body, Callback callback
)
{
  if (!hasEndpoint()) {
    QTimer::singleShot(0, this, [callback = std::move(callback)] {
      if (callback) {
        Reply reply;
        reply.error = tr("Syncthing is not running");
        callback(reply);
      }
    });
    return;
  }

  QUrl url = m_baseUrl;
  url.setPath(path);
  url.setQuery(query);

  QNetworkRequest request(url);
  request.setRawHeader(kApiKeyHeader, m_apiKey);
  request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
  request.setTransferTimeout(kRequestTimeoutMs);

  auto *reply = m_network.sendCustomRequest(request, verb, body);
  m_pending.insert(reply);
  connect(reply, &QNetworkReply::finished, this, [this, reply, callback = std::move(callback)] {
    m_pending.remove(reply);
    Reply result;
    result.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError) {
      result.error = reply->errorString();
      if (const auto detail = QString::fromUtf8(result.body).trimmed(); !detail.isEmpty()) {
        result.error = QStringLiteral("%1 (%2)").arg(result.error, detail);
      }
    }
    result.ok = reply->error() == QNetworkReply::NoError && result.status >= 200 && result.status < 300;
    reply->deleteLater();
    if (callback) {
      callback(result);
    }
  });
}

void SyncthingClient::get(const QString &path, const QUrlQuery &query, Callback callback)
{
  send(QByteArrayLiteral("GET"), path, query, {}, std::move(callback));
}

void SyncthingClient::post(const QString &path, const QUrlQuery &query, const QJsonValue &body, Callback callback)
{
  send(QByteArrayLiteral("POST"), path, query, encode(body), std::move(callback));
}

void SyncthingClient::put(const QString &path, const QUrlQuery &query, const QJsonValue &body, Callback callback)
{
  send(QByteArrayLiteral("PUT"), path, query, encode(body), std::move(callback));
}

void SyncthingClient::patch(const QString &path, const QUrlQuery &query, const QJsonValue &body, Callback callback)
{
  send(QByteArrayLiteral("PATCH"), path, query, encode(body), std::move(callback));
}

void SyncthingClient::del(const QString &path, const QUrlQuery &query, Callback callback)
{
  send(QByteArrayLiteral("DELETE"), path, query, {}, std::move(callback));
}

void SyncthingClient::ping(Callback callback)
{
  get(QStringLiteral("/rest/system/ping"), {}, std::move(callback));
}

void SyncthingClient::systemStatus(Callback callback)
{
  get(QStringLiteral("/rest/system/status"), {}, std::move(callback));
}

void SyncthingClient::connections(Callback callback)
{
  get(QStringLiteral("/rest/system/connections"), {}, std::move(callback));
}

void SyncthingClient::config(Callback callback)
{
  get(QStringLiteral("/rest/config"), {}, std::move(callback));
}

void SyncthingClient::dbStatus(const QString &folderId, Callback callback)
{
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("folder"), folderId);
  get(QStringLiteral("/rest/db/status"), query, std::move(callback));
}

void SyncthingClient::completion(const QString &deviceId, const QString &folderId, Callback callback)
{
  QUrlQuery query;
  if (!deviceId.isEmpty()) {
    query.addQueryItem(QStringLiteral("device"), deviceId);
  }
  if (!folderId.isEmpty()) {
    query.addQueryItem(QStringLiteral("folder"), folderId);
  }
  get(QStringLiteral("/rest/db/completion"), query, std::move(callback));
}

void SyncthingClient::systemErrors(Callback callback)
{
  get(QStringLiteral("/rest/system/error"), {}, std::move(callback));
}

void SyncthingClient::discovery(Callback callback)
{
  get(QStringLiteral("/rest/system/discovery"), {}, std::move(callback));
}

void SyncthingClient::pendingDevices(Callback callback)
{
  get(QStringLiteral("/rest/cluster/pending/devices"), {}, std::move(callback));
}

void SyncthingClient::setDevice(const QJsonObject &device, Callback callback)
{
  // POST on the collection adds the device or replaces the one with the same ID,
  // starting from the configured defaults for fields that are not given.
  post(QStringLiteral("/rest/config/devices"), {}, device, std::move(callback));
}

void SyncthingClient::setFolder(const QJsonObject &folder, Callback callback)
{
  post(QStringLiteral("/rest/config/folders"), {}, folder, std::move(callback));
}

void SyncthingClient::getFolder(const QString &folderId, Callback callback)
{
  get(QStringLiteral("/rest/config/folders/%1").arg(folderId), {}, std::move(callback));
}

void SyncthingClient::patchFolder(const QString &folderId, const QJsonObject &changes, Callback callback)
{
  patch(QStringLiteral("/rest/config/folders/%1").arg(folderId), {}, changes, std::move(callback));
}

void SyncthingClient::setDefaultFolder(const QJsonObject &changes, Callback callback)
{
  patch(QStringLiteral("/rest/config/defaults/folder"), {}, changes, std::move(callback));
}

void SyncthingClient::pauseAll(bool paused, Callback callback)
{
  const auto path = paused ? QStringLiteral("/rest/system/pause") : QStringLiteral("/rest/system/resume");
  post(path, {}, {}, std::move(callback));
}

void SyncthingClient::dismissPendingDevice(const QString &deviceId, Callback callback)
{
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("device"), deviceId);
  del(QStringLiteral("/rest/cluster/pending/devices"), query, std::move(callback));
}

void SyncthingClient::shutdown(Callback callback)
{
  post(QStringLiteral("/rest/system/shutdown"), {}, {}, std::move(callback));
}

} // namespace deskflow::gui::sync
