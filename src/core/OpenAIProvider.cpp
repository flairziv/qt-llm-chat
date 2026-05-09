#include "OpenAIProvider.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHttpMultiPart>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>

#include <functional>

namespace {

QString normalizedReasoningEffort(const QString &effort)
{
    return effort.trimmed().toLower();
}

bool supportsOpenAIReasoningEffort(const QString &model)
{
    return model.startsWith("gpt-5")
        || model.startsWith("o1")
        || model.startsWith("o3")
        || model.startsWith("o4");
}

bool isGptImageModel(const QString &model)
{
    const QString m = model.trimmed().toLower();
    return m.startsWith("gpt-image-2") || m.startsWith("gpt-image2");
}

bool useImagesEndpoint(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    return normalized == "images";
}

bool useImageEditsEndpoint(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    return normalized == "edits" || normalized == "edit" || normalized == "images_edits";
}

bool useResponsesEndpoint(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    return normalized == "responses" || normalized == "response";
}

QString buildEndpointUrl(const QString &baseUrl, const QString &path)
{
    QString base = baseUrl.trimmed();
    while (base.endsWith('/')) {
        base.chop(1);
    }

    if (base.endsWith("/v1", Qt::CaseInsensitive)
        && path.startsWith("/v1/", Qt::CaseInsensitive)) {
        return base + path.mid(3);
    }

    return base + path;
}

QString imageSizeFromEnv()
{
    const QString size = qEnvironmentVariable("LLMCHAT_OPENAI_IMAGE_SIZE").trimmed();
    return size.isEmpty() ? QStringLiteral("9:16") : size;
}

int imageCountFromEnv()
{
    bool ok = false;
    const int n = qEnvironmentVariableIntValue("LLMCHAT_OPENAI_IMAGE_N", &ok);
    return qBound(1, ok ? n : 1, 4);
}

int imageRequestTimeoutMs()
{
    bool ok = false;
    const int timeoutMs = qEnvironmentVariableIntValue("LLMCHAT_OPENAI_IMAGE_TIMEOUT_MS", &ok);
    return qBound(60000, ok ? timeoutMs : 600000, 1800000);
}

void applyTransferTimeout(QNetworkRequest &request, int timeoutMs)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    request.setTransferTimeout(timeoutMs);
#else
    Q_UNUSED(request);
    Q_UNUSED(timeoutMs);
#endif
}

QString generatedImagesDirPath()
{
    return QDir::cleanPath(QCoreApplication::applicationDirPath() + "/data/generated_images");
}

QString imageUrlHint(const QString &url)
{
    return QStringLiteral("\n[Generated Image URL] %1\n").arg(url);
}

QString imageFileHint(const QString &path)
{
    return QStringLiteral("\n[Generated Image Saved] %1\n").arg(QDir::toNativeSeparators(path));
}

QString parseErrorMessage(const QByteArray &payload, const QString &fallback)
{
    // 复用 LLMProvider 中统一的 API 错误解析（OpenAI / Claude / Gemini 三家格式相同）
    return LLMProvider::extractApiErrorMessage(payload, fallback);
}

QString extractLatestUserPrompt(const QList<ChatMessage> &messages)
{
    for (int i = messages.size() - 1; i >= 0; --i) {
        if (messages[i].role == "user") {
            const QString text = messages[i].content.trimmed();
            if (!text.isEmpty()) {
                return text;
            }
        }
    }

    if (!messages.isEmpty()) {
        return messages.last().content.trimmed();
    }

    return {};
}

bool looksLikeImageUrl(const QJsonObject &obj, const QString &url)
{
    const QString type = obj.value("type").toString().toLower();
    return type.contains("image")
        || obj.contains("image_url")
        || url.startsWith("http://", Qt::CaseInsensitive)
        || url.startsWith("https://", Qt::CaseInsensitive)
        || url.startsWith("data:image/", Qt::CaseInsensitive);
}

bool isInlineDataImage(const QString &value)
{
    return value.trimmed().startsWith("data:image/", Qt::CaseInsensitive);
}

QString normalizedImageBase64(QString value)
{
    value = value.trimmed();
    if (value.startsWith("data:image/", Qt::CaseInsensitive)) {
        const int comma = value.indexOf(',');
        if (comma >= 0) {
            value = value.mid(comma + 1);
        }
    }

    value.remove(QRegularExpression(QStringLiteral("\\s+")));
    return value;
}

bool looksLikeInlineBase64Image(const QString &value)
{
    if (isInlineDataImage(value)) {
        return true;
    }

    const QString normalized = normalizedImageBase64(value);
    if (normalized.length() < 128 || (normalized.length() % 4) != 0) {
        return false;
    }

    static const QRegularExpression pattern(QStringLiteral("^[A-Za-z0-9+/=]+$"));
    return pattern.match(normalized).hasMatch();
}

} // namespace

OpenAIProvider::OpenAIProvider(QNetworkAccessManager *nam,
                               const QString &apiKey,
                               const QString &baseUrl,
                               const QString &model,
                               const QString &reasoningEffort,
                               const QString &imageApiMode,
                               QObject *parent)
    : LLMProvider(nam, parent)
    , m_apiKey(apiKey)
    , m_baseUrl(baseUrl)
    , m_model(model)
    , m_reasoningEffort(reasoningEffort)
    , m_imageApiMode(imageApiMode)
{
}

// ============================================================================
// 入口：根据 m_imageApiMode 与模型名分派到三条独立路径
// ============================================================================

/**
 * @brief 流式聊天请求总入口（重写自 LLMProvider）
 *
 * 三条独立路径，根据 m_imageApiMode 与 m_model 派发：
 *   - "responses" → /v1/responses + image_generation tool（gpt-image-2 等）
 *   - "images"    → /v1/images/generations（DALL·E 经典端点）
 *   - 其他       → /v1/chat/completions（普通对话；图片模型走非流式 JSON，否则 SSE 流式）
 *
 * 进入前清理三块状态：累积响应、SSE 解析器缓冲、图片去重哈希集合。
 * API Key 校验由 ensureApiKeyConfigured() 统一处理（仅在 baseUrl 指向 openai.com 时强制）。
 */
void OpenAIProvider::doSendStreamingRequest(
    const QList<ChatMessage> &messages,
    const QString &systemPrompt,
    int maxTokens,
    double temperature)
{
    m_accumulatedResponse.clear();
    m_sseParser.reset();
    m_emittedImageHashes.clear();

    if (!ensureApiKeyConfigured()) return;

    if (useResponsesEndpoint(m_imageApiMode)) {
        sendResponsesImageRequest(messages);
        return;
    }
    if (useImagesEndpoint(m_imageApiMode)) {
        sendImagesGenerationRequest(messages);
        return;
    }
    if (useImageEditsEndpoint(m_imageApiMode)) {
        sendImagesEditRequest(messages);
        return;
    }
    sendChatRequest(messages, systemPrompt, maxTokens, temperature);
}

// ============================================================================
// 鉴权与请求构造
// ============================================================================

/**
 * @brief 校验 API Key 是否已配置
 *
 * 仅在 baseUrl 指向 openai.com 且非局域网（localhost / 127. / 192.168. / 10.0.）
 * 时才强制要求 Key——本地 Ollama / 自建代理通常不需要 Key。
 *
 * @return true 可继续发请求；false 已 emit errorOccurred，调用方应直接返回
 */
bool OpenAIProvider::ensureApiKeyConfigured()
{
    if (!m_apiKey.isEmpty()) return true;

    const bool isLocal = m_baseUrl.contains("localhost") || m_baseUrl.contains("127.0.0.1")
                         || m_baseUrl.contains("192.168.") || m_baseUrl.contains("10.0.");
    if (!isLocal && m_baseUrl.contains("openai.com")) {
        emit errorOccurred("OpenAI API key is not configured. Please set it in Settings.");
        return false;
    }
    return true;
}

/**
 * @brief 在基类 makeJsonRequest 之上追加 OpenAI 专属的鉴权头与可选超时
 *
 * @param url               目标端点 URL
 * @param applyImageTimeout 是否启用长超时（图片生成耗时较长，默认 false=用 Qt 默认超时）
 */
QNetworkRequest OpenAIProvider::buildAuthorizedRequest(const QUrl &url, bool applyImageTimeout)
{
    QNetworkRequest request = makeJsonRequest(url);
    if (applyImageTimeout) {
        applyTransferTimeout(request, imageRequestTimeoutMs());
    }
    if (!m_apiKey.isEmpty()) {
        request.setRawHeader("Authorization", ("Bearer " + m_apiKey).toUtf8());
    }
    return request;
}

// ============================================================================
// 图片输出去重
// ============================================================================
// 各路径都需要按"已落盘文件路径"或"已发出 URL"做去重，避免同一图被多源（url/b64_json/...
// 等多个字段）重复发出。两个 helper 用 SHA-1 摘要做 key，命中即跳过。

/**
 * @brief 尝试把已落盘的图片路径写入输出文本（去重后）
 *
 * @param savedPath 落盘后的本地路径；空字符串直接跳过
 * @param output    [in/out] 累积的输出文本，命中新路径时追加 "[Generated Image Saved] ..."
 * @return true 表示新增了一条；false 表示空或已被发过
 */
bool OpenAIProvider::tryEmitImagePath(const QString &savedPath, QString &output)
{
    if (savedPath.isEmpty()) return false;
    const QByteArray key = "path:"
        + QCryptographicHash::hash(savedPath.toUtf8(), QCryptographicHash::Sha1).toHex();
    if (m_emittedImageHashes.contains(key)) return false;
    m_emittedImageHashes.insert(key);
    output += imageFileHint(savedPath);
    return true;
}

/**
 * @brief 尝试把图片 URL 写入输出文本（去重后）
 *
 * @param url    图片直链；空字符串直接跳过
 * @param output [in/out] 累积输出，命中新 URL 时追加 "[Generated Image URL] ..."
 * @return true 新增；false 空或已发
 */
bool OpenAIProvider::tryEmitImageUrl(const QString &url, QString &output)
{
    if (url.isEmpty()) return false;
    const QByteArray key = "url:"
        + QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex();
    if (m_emittedImageHashes.contains(key)) return false;
    m_emittedImageHashes.insert(key);
    output += imageUrlHint(url);
    return true;
}

// ============================================================================
// 非流式 image 请求的统一 reply 生命周期
// ============================================================================
// 三条 image 路径的 finished lambda 之前各写一份 abort-race 检查、HTTP 错误处理、
// 成功后 emit tokenReceived/responseFinished 的样板。这里收敛为唯一一份，每条路径
// 只需提供一个 payload→展示文本的解析回调。

/**
 * @brief 发送一个非流式 POST 并统一处理 reply 生命周期
 *
 * 工作流程：
 * 1. POST request + body，把 reply 记到 m_currentReply
 * 2. 在 reply 完成时（lambda 里）：
 *    - 若 m_currentReply 已被 abort()/被新请求顶替，安静 deleteLater 退出
 *    - 若有 HTTP/网络错误，从 payload 提取 API 错误信息后 emit errorOccurred
 *    - 否则调 parsePayload 解析展示文本，emit tokenReceived + responseFinished
 *    - 最终统一清理 reply / sse parser / 累积响应
 *
 * @param request      已构造好（含鉴权头）的请求对象
 * @param body         JSON 序列化后的请求体
 * @param parsePayload payload→展示文本 的解析回调，由各路径各自提供
 */
void OpenAIProvider::postImageRequest(
    const QNetworkRequest &request,
    const QByteArray &body,
    std::function<QString(const QByteArray&)> parsePayload,
    int placeholderCount)
{
    QNetworkReply *reply = m_nam->post(request, body);
    m_currentReply = reply;

    // 通知 UI 显示 shimmer 占位符（应在 connect 之前 emit，确保占位符立刻可见）
    emit imageGenerationStarted(qMax(1, placeholderCount));

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, parsePayload = std::move(parsePayload)]() {
        // abort 与新请求竞态：当前 reply 已不是 m_currentReply 时安静释放即可
        if (reply != m_currentReply) {
            reply->deleteLater();
            return;
        }

        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(parseErrorMessage(payload, reply->errorString()));
            reply->deleteLater();
            m_currentReply = nullptr;
            m_sseParser.reset();
            m_accumulatedResponse.clear();
            return;
        }

        QString output = parsePayload(payload);
        if (output.isEmpty()) {
            output = QStringLiteral("[Image generation succeeded but no image payload was returned.]");
        }

        m_accumulatedResponse = output;
        emit tokenReceived(output);
        emit responseFinished(output);

        reply->deleteLater();
        m_currentReply = nullptr;
        m_sseParser.reset();
    });
}

// ============================================================================
// 路径 1：/v1/responses + image_generation tool（gpt-image-2 等通过 Responses API）
// ============================================================================

/**
 * @brief 发起 Responses API 图片生成请求（支持参考图附件 / 多模态 input）
 *
 * 请求体形如：
 *   {
 *     model,
 *     input: [{ role:"user", content:[
 *                 {type:"input_text",  text:"..."},
 *                 {type:"input_image", image_url:"data:image/png;base64,..."}
 *             ]}],
 *     tools: [{type:"image_generation"}],
 *     tool_choice: "auto"
 *   }
 *
 * 仅取最后一条 user 消息（Responses 端点目前按单轮处理）：
 *   - text 块：消息正文 + flattenTextAttachments 拼接的 TextFile 内容
 *   - input_image 块：每个 Attachment::Image 走 data URI 内联 base64
 *   - Document 类型不能作为视觉输入，转成文字提示放进 text 块
 *
 * 注意 Responses API 的 content 块类型用 "input_text" / "input_image" 前缀，
 * 跟 chat completions 的 "text" / "image_url" 不同；image_url 直接是字符串
 * 而不是再嵌一层 {url:...} 对象。
 */
void OpenAIProvider::sendResponsesImageRequest(const QList<ChatMessage> &messages)
{
    // 找最后一条 user 消息（含附件），允许 content 为空但 attachments 非空
    int lastUserIdx = -1;
    for (int i = messages.size() - 1; i >= 0; --i) {
        if (messages[i].role == "user") { lastUserIdx = i; break; }
    }
    if (lastUserIdx < 0) {
        emit errorOccurred("Image prompt is empty.");
        return;
    }
    const ChatMessage &last = messages[lastUserIdx];

    // 拼接文本：消息正文 + TextFile 附件（flattenTextAttachments）+ Document 提示
    QString textPart;
    for (const auto &att : last.attachments) {
        if (att.type == Attachment::Document) {
            textPart += QStringLiteral("[Document: %1 (binary, not displayed)]\n\n")
                            .arg(att.fileName);
        }
    }
    textPart += flattenTextAttachments(last.attachments);
    textPart += last.content;

    // 构造 input.content 数组
    QJsonArray contentArray;
    if (!textPart.trimmed().isEmpty()) {
        contentArray.append(QJsonObject{
            {"type", "input_text"},
            {"text", textPart},
        });
    }
    for (const auto &att : last.attachments) {
        if (att.type != Attachment::Image) continue;
        if (att.fileData.isEmpty()) continue;
        const QString dataUri = QStringLiteral("data:%1;base64,%2")
            .arg(att.mimeType.isEmpty() ? QStringLiteral("image/png") : att.mimeType,
                 QString::fromLatin1(att.fileData.toBase64()));
        contentArray.append(QJsonObject{
            {"type", "input_image"},
            {"image_url", dataUri},
        });
    }
    if (contentArray.isEmpty()) {
        emit errorOccurred("Image prompt is empty.");
        return;
    }

    QJsonArray inputArray;
    inputArray.append(QJsonObject{
        {"role", "user"},
        {"content", contentArray},
    });

    QNetworkRequest request = buildAuthorizedRequest(
        QUrl(buildEndpointUrl(m_baseUrl, "/v1/responses")), /*applyImageTimeout=*/true);

    QJsonObject body;
    body["model"] = m_model;
    body["input"] = inputArray;

    QJsonArray tools;
    QJsonObject imageTool;
    imageTool["type"] = "image_generation";
    tools.append(imageTool);
    body["tools"] = tools;
    body["tool_choice"] = "auto";

    postImageRequest(request, QJsonDocument(body).toJson(),
        [this](const QByteArray &payload) { return extractResponsesPayload(payload); });
}

/**
 * @brief 解析 Responses API 的响应 payload
 *
 * 响应顶层 output[] 中查找 type=="image_generation_call" 的项，
 * 然后递归走查 result / data / image / images 等字段中的 base64 字符串。
 * 每条 base64 都尝试落盘并通过 tryEmitImagePath 去重输出。
 */
QString OpenAIProvider::extractResponsesPayload(const QByteArray &payload)
{
    QString output;

    std::function<void(const QJsonValue &)> collect;
    collect = [this, &output, &collect](const QJsonValue &value) {
        if (value.isNull() || value.isUndefined()) return;

        if (value.isString()) {
            tryEmitImagePath(saveBase64ImageToFile(value.toString()), output);
            return;
        }
        if (value.isArray()) {
            for (const auto &item : value.toArray()) collect(item);
            return;
        }
        if (!value.isObject()) return;

        const QJsonObject obj = value.toObject();
        tryEmitImagePath(saveBase64ImageToFile(obj.value("result").toString()), output);
        tryEmitImagePath(saveBase64ImageToFile(obj.value("b64_json").toString()), output);
        tryEmitImagePath(saveBase64ImageToFile(obj.value("base64").toString()), output);
        tryEmitImagePath(saveBase64ImageToFile(obj.value("image_base64").toString()), output);
        tryEmitImagePath(saveBase64ImageToFile(obj.value("data").toString()), output);

        collect(obj.value("result"));
        collect(obj.value("data"));
        collect(obj.value("image"));
        collect(obj.value("images"));
    };

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isObject()) {
        const QJsonArray outputArray = doc.object().value("output").toArray();
        for (const QJsonValue &itemValue : outputArray) {
            if (!itemValue.isObject()) continue;
            const QJsonObject item = itemValue.toObject();
            if (item.value("type").toString() == QLatin1String("image_generation_call")) {
                collect(item.value("result"));
                collect(item);
            }
        }
    }
    return output;
}

// ============================================================================
// 路径 2：/v1/images/generations（DALL·E 经典 image 端点）
// ============================================================================

/**
 * @brief 发起 Images Generations 端点请求
 *
 * 请求体：{ model, prompt, n, size }，n 与 size 由环境变量
 * LLMCHAT_OPENAI_IMAGE_N / LLMCHAT_OPENAI_IMAGE_SIZE 控制（默认 1 / "9:16"）。
 */
void OpenAIProvider::sendImagesGenerationRequest(const QList<ChatMessage> &messages)
{
    const QString prompt = extractLatestUserPrompt(messages);
    if (prompt.isEmpty()) {
        emit errorOccurred("Image prompt is empty.");
        return;
    }

    QNetworkRequest request = buildAuthorizedRequest(
        QUrl(buildEndpointUrl(m_baseUrl, "/v1/images/generations")), /*applyImageTimeout=*/true);

    QJsonObject body;
    body["model"] = m_model;
    body["prompt"] = prompt;
    body["n"] = imageCountFromEnv();
    body["size"] = imageSizeFromEnv();

    postImageRequest(request, QJsonDocument(body).toJson(),
        [this](const QByteArray &payload) { return extractImagesPayload(payload); },
        imageCountFromEnv());
}

// ============================================================================
// 路径 3：/v1/images/edits（参考图 / 以图生图，multipart/form-data）
// ============================================================================

void OpenAIProvider::sendImagesEditRequest(const QList<ChatMessage> &messages)
{
    int lastUserIdx = -1;
    for (int i = messages.size() - 1; i >= 0; --i) {
        if (messages[i].role == "user") { lastUserIdx = i; break; }
    }
    if (lastUserIdx < 0) {
        emit errorOccurred("Image prompt is empty.");
        return;
    }

    const ChatMessage &last = messages[lastUserIdx];
    const QString prompt = (flattenTextAttachments(last.attachments) + last.content).trimmed();
    if (prompt.isEmpty()) {
        emit errorOccurred("Image prompt is empty.");
        return;
    }

    QNetworkRequest request = buildAuthorizedRequest(
        QUrl(buildEndpointUrl(m_baseUrl, "/v1/images/edits")), /*applyImageTimeout=*/true);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QVariant());

    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    auto appendTextPart = [multiPart](const QByteArray &name, const QString &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"").arg(QString::fromLatin1(name)));
        part.setBody(value.toUtf8());
        multiPart->append(part);
    };

    appendTextPart("model", m_model);
    appendTextPart("prompt", prompt);
    appendTextPart("n", QString::number(imageCountFromEnv()));
    appendTextPart("size", imageSizeFromEnv());

    int imageCount = 0;
    for (const auto &att : last.attachments) {
        if (att.type != Attachment::Image || att.fileData.isEmpty()) continue;
        const QString fileName = att.fileName.isEmpty()
            ? QStringLiteral("image_%1.png").arg(imageCount + 1)
            : att.fileName;

        QHttpPart imagePart;
        imagePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                            QStringLiteral("form-data; name=\"image[]\"; filename=\"%1\"").arg(fileName));
        imagePart.setHeader(QNetworkRequest::ContentTypeHeader,
                            att.mimeType.isEmpty() ? QStringLiteral("image/png") : att.mimeType);
        imagePart.setBody(att.fileData);
        multiPart->append(imagePart);
        ++imageCount;
    }

    if (imageCount == 0) {
        multiPart->deleteLater();
        emit errorOccurred("Images edits API requires at least one image attachment.");
        return;
    }

    QNetworkReply *reply = m_nam->post(request, multiPart);
    multiPart->setParent(reply);
    m_currentReply = reply;
    emit imageGenerationStarted(qMax(1, imageCountFromEnv()));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        if (reply != m_currentReply) {
            reply->deleteLater();
            return;
        }

        const QByteArray payload = reply->readAll();
        if (reply->error() != QNetworkReply::NoError) {
            emit errorOccurred(parseErrorMessage(payload, reply->errorString()));
            reply->deleteLater();
            m_currentReply = nullptr;
            m_sseParser.reset();
            m_accumulatedResponse.clear();
            return;
        }

        QString output = extractImagesPayload(payload);
        if (output.isEmpty()) {
            output = QStringLiteral("[Image edit succeeded but no image payload was returned.]");
        }
        m_accumulatedResponse = output;
        emit tokenReceived(output);
        emit responseFinished(output);

        reply->deleteLater();
        m_currentReply = nullptr;
        m_sseParser.reset();
    });
}

/**
 * @brief 解析 /v1/images/generations 响应
 *
 * 响应顶层 data[] 中每项可能含 url（http 直链 / inline data:）/ b64_json / base64 /
 * image_base64 / image_url。逐字段尝试落盘并经 tryEmit* 去重输出。
 */
QString OpenAIProvider::extractImagesPayload(const QByteArray &payload)
{
    QString output;
    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (!doc.isObject()) return output;

    const QJsonArray dataArray = doc.object().value("data").toArray();
    for (const QJsonValue &val : dataArray) {
        const QJsonObject item = val.toObject();

        const QString url = item.value("url").toString().trimmed();
        if (!url.isEmpty()) {
            if (isInlineDataImage(url)) {
                tryEmitImagePath(saveBase64ImageToFile(url), output);
            } else {
                tryEmitImageUrl(url, output);
            }
        }

        // 不同 API 网关返回字段名不一致，三个候选都试一遍
        tryEmitImagePath(saveBase64ImageToFile(item.value("b64_json").toString()), output);
        tryEmitImagePath(saveBase64ImageToFile(item.value("base64").toString()), output);
        tryEmitImagePath(saveBase64ImageToFile(item.value("image_base64").toString()), output);

        const QJsonValue imageUrl = item.value("image_url");
        if (imageUrl.isString()) {
            tryEmitImagePath(saveBase64ImageToFile(imageUrl.toString()), output);
        } else if (imageUrl.isObject()) {
            tryEmitImagePath(
                saveBase64ImageToFile(imageUrl.toObject().value("url").toString()), output);
        }
    }
    return output;
}

// ============================================================================
// 路径 3：/v1/chat/completions（普通对话 SSE 流式 + 图片模型走非流式 JSON）
// ============================================================================

/**
 * @brief 发起 /v1/chat/completions 请求
 *
 * 这是 OpenAI 的"对话补全"主端点。两种模式：
 *   - 图片模型（gpt-image-2 系列）：stream=false，非流式返回完整 JSON，
 *     用 postImageRequest 走统一 reply 生命周期，payload 用 extractChatImagePayload 宽匹配
 *   - 普通对话模型：stream=true，SSE 流式，用基类 connectReplySignals 接管
 *
 * 消息体支持多模态：每条消息若含 attachment，content 为数组（image_url / text 块），
 * 否则为纯字符串。Document 类型不支持原生上传，退化为文本提示；TextFile 走 flattenTextAttachments。
 */
void OpenAIProvider::sendChatRequest(
    const QList<ChatMessage> &messages,
    const QString &systemPrompt,
    int maxTokens,
    double temperature)
{
    const bool imageModel = isGptImageModel(m_model);

    QNetworkRequest request = buildAuthorizedRequest(
        QUrl(buildEndpointUrl(m_baseUrl, "/v1/chat/completions")),
        /*applyImageTimeout=*/imageModel);

    QJsonObject body;
    body["model"] = m_model;
    body["stream"] = !imageModel;

    if (imageModel) {
        body["size"] = imageSizeFromEnv();
        body["n"] = imageCountFromEnv();
    } else {
        body["max_tokens"] = maxTokens;
        body["temperature"] = temperature;

        const QString reasoningEffort = normalizedReasoningEffort(m_reasoningEffort);
        if (!reasoningEffort.isEmpty()
            && reasoningEffort != "default"
            && supportsOpenAIReasoningEffort(m_model)) {
            body["reasoning_effort"] = reasoningEffort;
        }
    }

    QJsonArray msgArray;
    if (!systemPrompt.isEmpty()) {
        QJsonObject sysMsg;
        sysMsg["role"] = "system";
        sysMsg["content"] = systemPrompt;
        msgArray.append(sysMsg);
    }

    for (const auto &msg : messages) {
        QJsonObject m;
        m["role"] = msg.role;

        if (msg.attachments.isEmpty()) {
            m["content"] = msg.content;
        } else {
            QJsonArray contentArray;
            QString textParts;

            for (const auto &att : msg.attachments) {
                if (att.type == Attachment::Image) {
                    QJsonObject imageBlock;
                    imageBlock["type"] = "image_url";
                    QJsonObject imageUrl;
                    imageUrl["url"] = QStringLiteral("data:%1;base64,%2")
                        .arg(att.mimeType,
                             QString::fromLatin1(att.fileData.toBase64()));
                    imageBlock["image_url"] = imageUrl;
                    contentArray.append(imageBlock);
                } else if (att.type == Attachment::Document) {
                    textParts += QStringLiteral("[Document: %1 (binary, not displayed)]\n\n")
                        .arg(att.fileName);
                }
                // TextFile 由 flattenTextAttachments 统一处理（见下方）
            }

            QString fullText = textParts + flattenTextAttachments(msg.attachments) + msg.content;
            if (!fullText.trimmed().isEmpty()) {
                QJsonObject textBlock;
                textBlock["type"] = "text";
                textBlock["text"] = fullText;
                contentArray.append(textBlock);
            }

            m["content"] = contentArray;
        }

        msgArray.append(m);
    }
    body["messages"] = msgArray;

    if (imageModel) {
        // 图片模型：非流式，宽匹配地从响应里找出图片 URL / base64
        postImageRequest(request, QJsonDocument(body).toJson(),
            [this](const QByteArray &payload) { return extractChatImagePayload(payload); });
    } else {
        // 普通对话：流式 SSE，走基类统一的 reply 信号路径
        QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson());
        connectReplySignals(reply);
    }
}

/**
 * @brief 解析图片模型走 chat/completions 时的非流式响应
 *
 * 图片模型（gpt-image-2 系列）走 chat/completions 时，响应 JSON 形态
 * 比 /v1/images/generations 更杂——不同代理实现把图片塞在 choices[].message.content、
 * data[]、output[]、images[] 等位置。这里采用**最宽匹配**：递归走查根对象下的
 * data / content / output / images / items / message / choices，遇到字符串/对象都
 * 尝试当图片处理（base64 / URL / data: 前缀），同时也保留 obj.text / obj.content
 * 这类纯文本字段（gpt-image-2 偶尔会随图返回一段说明）。
 *
 * 经 tryEmit* 去重后输出，避免同一图被多个字段命中重复输出。
 */
QString OpenAIProvider::extractChatImagePayload(const QByteArray &payload)
{
    QString output;

    auto collectImageString = [this, &output](const QString &text) {
        const QString value = text.trimmed();
        if (value.isEmpty()) return;

        if (isInlineDataImage(value) || looksLikeInlineBase64Image(value)) {
            tryEmitImagePath(saveBase64ImageToFile(value), output);
            return;
        }
        if (value.startsWith("http://", Qt::CaseInsensitive)
            || value.startsWith("https://", Qt::CaseInsensitive)) {
            tryEmitImageUrl(value, output);
        }
    };

    std::function<void(const QJsonValue &)> collect;
    collect = [this, &output, &collect, &collectImageString](const QJsonValue &value) {
        if (value.isNull() || value.isUndefined()) return;

        if (value.isString()) {
            collectImageString(value.toString());
            return;
        }
        if (value.isArray()) {
            for (const auto &item : value.toArray()) collect(item);
            return;
        }
        if (!value.isObject()) return;

        const QJsonObject obj = value.toObject();

        const QString url = obj.value("url").toString().trimmed();
        if (!url.isEmpty() && !isInlineDataImage(url)) {
            tryEmitImageUrl(url, output);
        }
        collectImageString(url);

        tryEmitImagePath(saveBase64ImageToFile(obj.value("b64_json").toString()), output);
        tryEmitImagePath(saveBase64ImageToFile(obj.value("base64").toString()), output);
        tryEmitImagePath(saveBase64ImageToFile(obj.value("image_base64").toString()), output);

        collect(obj.value("data"));
        collect(obj.value("content"));
        collect(obj.value("output"));
        collect(obj.value("images"));
        collect(obj.value("items"));
        collect(obj.value("message"));
        collect(obj.value("choices"));
    };

    const QJsonDocument doc = QJsonDocument::fromJson(payload);
    if (doc.isObject()) {
        const QJsonObject root = doc.object();
        collect(root.value("data"));
        collect(root.value("content"));
        collect(root.value("output"));
        collect(root.value("images"));
        collect(root.value("items"));
        collect(root.value("message"));
        collect(root.value("choices"));
    }
    return output;
}

LLMProviderParseResult OpenAIProvider::parseSSEData(const QByteArray &data)
{
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        return {};
    }

    const QJsonObject root = doc.object();

    // 先把 reasoning 增量从 choices[*].delta.reasoning_content / .reasoning 里抽出来。
    // 不要在这里 return：同一 event 里可能 *同时* 有 content（部分代理 / 实验性 API
    // 这么发），早 return 会丢正文，导致 cleanResponse 为空 → TTS 不出声。
    LLMProviderParseResult result;
    for (const auto &c : root.value("choices").toArray()) {
        const QJsonObject delta = c.toObject().value("delta").toObject();
        const QString rc = delta.value("reasoning_content").toString();
        if (!rc.isEmpty()) result.reasoningToken += rc;
        const QJsonValue rv = delta.value("reasoning");
        if (rv.isString()) result.reasoningToken += rv.toString();
    }

    QString token;

    auto appendUrl = [&](const QString &url) {
        const QString u = url.trimmed();
        if (u.isEmpty()) {
            return;
        }
        if (isInlineDataImage(u)) {
            const QString savedPath = saveBase64ImageToFile(u);
            if (!savedPath.isEmpty()) {
                const QByteArray key = "path:" + QCryptographicHash::hash(savedPath.toUtf8(), QCryptographicHash::Sha1).toHex();
                if (!m_emittedImageHashes.contains(key)) {
                    m_emittedImageHashes.insert(key);
                    token += imageFileHint(savedPath);
                }
            }
            return;
        }
        const QByteArray key = "url:" + QCryptographicHash::hash(u.toUtf8(), QCryptographicHash::Sha1).toHex();
        if (m_emittedImageHashes.contains(key)) {
            return;
        }
        m_emittedImageHashes.insert(key);
        token += imageUrlHint(u);
    };

    auto appendSaved = [&](const QString &path) {
        if (path.isEmpty()) {
            return;
        }
        const QByteArray key = "path:" + QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex();
        if (m_emittedImageHashes.contains(key)) {
            return;
        }
        m_emittedImageHashes.insert(key);
        token += imageFileHint(path);
    };

    auto appendB64 = [&](const QString &b64) {
        if (!looksLikeInlineBase64Image(b64)) {
            return;
        }
        const QString path = saveBase64ImageToFile(b64);
        if (!path.isEmpty()) {
            appendSaved(path);
        }
    };

    std::function<void(const QJsonValue &)> walk;
    walk = [&](const QJsonValue &v) {
        if (v.isNull() || v.isUndefined()) {
            return;
        }
        if (v.isString()) {
            const QString text = v.toString();
            if (looksLikeInlineBase64Image(text)) {
                appendB64(text);
            } else {
                token += text;
            }
            return;
        }
        if (v.isArray()) {
            for (const auto &item : v.toArray()) {
                walk(item);
            }
            return;
        }
        if (!v.isObject()) {
            return;
        }

        const QJsonObject obj = v.toObject();

        const QString text = obj.value("text").toString();
        if (!text.isEmpty()) {
            token += text;
        }

        const QString revisedPrompt = obj.value("revised_prompt").toString();
        if (!revisedPrompt.isEmpty()) {
            token += QStringLiteral("\n[Revised Prompt] %1\n").arg(revisedPrompt);
        }

        const QJsonValue content = obj.value("content");
        if (content.isString()) {
            token += content.toString();
        } else if (!content.isUndefined() && !content.isNull()) {
            walk(content);
        }

        const QJsonValue imageUrl = obj.value("image_url");
        if (imageUrl.isString()) {
            appendUrl(imageUrl.toString());
        } else if (imageUrl.isObject()) {
            const QJsonObject imageObj = imageUrl.toObject();
            appendUrl(imageObj.value("url").toString());
            appendB64(imageObj.value("b64_json").toString());
            appendB64(imageObj.value("base64").toString());
            appendB64(imageObj.value("image_base64").toString());
        }

        const QString directUrl = obj.value("url").toString();
        if (!directUrl.isEmpty() && looksLikeImageUrl(obj, directUrl)) {
            appendUrl(directUrl);
        }

        appendB64(obj.value("b64_json").toString());
        appendB64(obj.value("base64").toString());
        appendB64(obj.value("image_base64").toString());

        static const char *nestedKeys[] = {
            "delta", "message", "data", "images", "image",
            "output", "outputs", "parts", "items"
        };
        for (const char *key : nestedKeys) {
            const QJsonValue nested = obj.value(QLatin1String(key));
            if (!nested.isUndefined() && !nested.isNull()) {
                walk(nested);
            }
        }
    };

    walk(root.value("choices"));
    if (token.isEmpty()) {
        walk(root.value("data"));
        walk(root.value("message"));
        walk(root.value("output"));
        walk(root.value("content"));
        walk(root.value("delta"));
        walk(root.value("image_url"));
        walk(root.value("b64_json"));
    }

    result.contentToken = token;
    return result;
}

QString OpenAIProvider::saveBase64ImageToFile(const QString &base64Data)
{
    const QString normalized = normalizedImageBase64(base64Data);
    if (normalized.isEmpty()) {
        return {};
    }

    const QByteArray bytes = QByteArray::fromBase64(normalized.toLatin1());
    if (bytes.isEmpty()) {
        return {};
    }
    return saveImageBytesToFile(bytes);
}

QString OpenAIProvider::saveImageBytesToFile(const QByteArray &bytes)
{
    if (bytes.isEmpty()) {
        return {};
    }

    const QByteArray digestKey = "img:" + QCryptographicHash::hash(bytes, QCryptographicHash::Sha1).toHex();
    if (m_emittedImageHashes.contains(digestKey)) {
        return {};
    }

    QImage image;
    if (!image.loadFromData(bytes)) {
        return {};
    }

    QDir outputDir(generatedImagesDirPath());
    if (!outputDir.exists() && !outputDir.mkpath(".")) {
        return {};
    }

    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    QString filePath = outputDir.filePath(QStringLiteral("gpt-image-2_%1.png").arg(timestamp));
    int suffix = 1;
    while (QFile::exists(filePath)) {
        filePath = outputDir.filePath(QStringLiteral("gpt-image-2_%1_%2.png").arg(timestamp).arg(suffix++));
    }

    if (!image.save(filePath, "PNG")) {
        return {};
    }

    m_emittedImageHashes.insert(digestKey);
    return filePath;
}
