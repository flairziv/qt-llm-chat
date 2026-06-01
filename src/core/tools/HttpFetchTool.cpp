#include "HttpFetchTool.h"

#include "core/NetworkRequestUtils.h"
#include "core/Tool.h"
#include "core/ToolRegistry.h"

#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

namespace {

constexpr int kDefaultTimeoutMs = 15000;
constexpr qint64 kMaxResponseBytes = 5 * 1024 * 1024;  // 5 MiB

/**
 * @brief fetch_url(url: string, timeout_ms?: integer) → 响应正文（首行 HTTP 状态码）
 *
 * 同步 GET（C8 commit 会改为 QFutureWatcher 异步执行 + Esc 中止）。
 *
 * 错误策略：
 *   - 4xx / 5xx 响应仍然算"成功"（isError=false），首行写入 HTTP 状态码、
 *     正文跟在后面，让模型自行判断要怎么用。粗暴把 4xx 转成 isError 反而丢
 *     上下文（模型经常需要看返回体里的错误说明决定换条路径）。
 *   - isError=true 只用在：网络层失败 / 超时 / 输入校验失败 / 超过大小上限。
 */
ToolResult fetchUrl(const QJsonObject &args)
{
    ToolResult r;

    const QString urlStr = args.value(QStringLiteral("url")).toString();
    if (urlStr.isEmpty()) {
        r.isError = true;
        r.content = QStringLiteral("Missing required argument: url");
        return r;
    }
    const QUrl url(urlStr);
    if (!url.isValid()
        || (url.scheme() != QStringLiteral("http")
            && url.scheme() != QStringLiteral("https"))) {
        r.isError = true;
        r.content = QStringLiteral(
                        "Invalid or unsupported URL (must be http:// or https://): %1")
                        .arg(urlStr);
        return r;
    }

    const int timeoutMs = args.value(QStringLiteral("timeout_ms")).toInt(kDefaultTimeoutMs);

    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    applyGoogleChromeUserAgent(req);
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    QNetworkReply *reply = nam.get(req);

    // 用 QEventLoop 同步等：reply.finished 和 timer.timeout 任一触发都退出
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();

    const bool timedOut = !timer.isActive() && !reply->isFinished();
    timer.stop();

    if (timedOut) {
        reply->abort();
        reply->deleteLater();
        r.isError = true;
        r.content = QStringLiteral("Request timed out after %1 ms").arg(timeoutMs);
        return r;
    }

    if (reply->error() != QNetworkReply::NoError
        // 4xx/5xx 不当成网络错误：QNetworkReply 会同时设 error 和 statusCode，
        // 这里只在没拿到 HTTP 状态码时才报网络层错。
        && !reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()) {
        const QString err = reply->errorString();
        reply->deleteLater();
        r.isError = true;
        r.content = QStringLiteral("Network error: %1").arg(err);
        return r;
    }

    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    if (body.size() > kMaxResponseBytes) {
        r.isError = true;
        r.content = QStringLiteral("Response too large (%1 bytes); fetch_url caps at %2 bytes")
                        .arg(body.size())
                        .arg(kMaxResponseBytes);
        return r;
    }

    r.content = QStringLiteral("HTTP %1\n\n%2").arg(statusCode).arg(QString::fromUtf8(body));
    return r;
}

// 内联一遍 schema 构造 helper（与 FileSystemTools.cpp 里同名 helper 重复）。
// 工具数量积累到一定程度再抽到共用头，当前两份重复成本可忽略。
QJsonObject makeObjectSchema(const QJsonObject &properties, const QStringList &required)
{
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = properties;
    if (!required.isEmpty()) {
        QJsonArray arr;
        for (const auto &r : required) arr.append(r);
        schema[QStringLiteral("required")] = arr;
    }
    return schema;
}

QJsonObject makeStringProperty(const QString &description)
{
    QJsonObject p;
    p[QStringLiteral("type")] = QStringLiteral("string");
    p[QStringLiteral("description")] = description;
    return p;
}

QJsonObject makeIntegerProperty(const QString &description)
{
    QJsonObject p;
    p[QStringLiteral("type")] = QStringLiteral("integer");
    p[QStringLiteral("description")] = description;
    return p;
}

}  // namespace

void registerHttpFetchTool()
{
    Tool t;
    t.name = QStringLiteral("fetch_url");
    t.description = QStringLiteral(
        "Fetch a URL via HTTP/HTTPS GET and return the response body as text. "
        "The first line is \"HTTP <status>\" followed by a blank line and the body. "
        "Response size is capped at 5 MiB.");

    QJsonObject props;
    props[QStringLiteral("url")] = makeStringProperty(
        QStringLiteral("The full http:// or https:// URL to fetch."));
    props[QStringLiteral("timeout_ms")] = makeIntegerProperty(
        QStringLiteral("Optional request timeout in milliseconds. Default 15000."));
    t.inputSchema = makeObjectSchema(props, { QStringLiteral("url") });

    // fetch_url 能对任意地址发起请求，按 ShellOrNetwork 处理：执行前弹审批，
    // 并提供"本会话允许"。审批门见 MainWindow::approveToolCall（C6）。
    t.riskLevel = RiskLevel::ShellOrNetwork;
    t.execute = fetchUrl;
    ToolRegistry::instance().registerTool(std::move(t));
}
