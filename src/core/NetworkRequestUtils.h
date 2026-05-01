#pragma once

#include <QNetworkRequest>

/**
 * @brief 给 QNetworkRequest 设置 Chrome User-Agent 头
 *
 * 部分 LLM 中转 / 代理服务（特别是 Cloudflare 后面的）会拒绝没有
 * 浏览器风格 UA 的请求，统一在这里加 Chrome UA 避免被拦。
 *
 * 内联函数，header-only —— 直接 #include 后就能用。
 */
inline void applyGoogleChromeUserAgent(QNetworkRequest &request)
{
    request.setRawHeader(
        "User-Agent",
        QByteArrayLiteral(
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
            "(KHTML, like Gecko) Chrome/143.0.0.0 Safari/537.36"));
}
