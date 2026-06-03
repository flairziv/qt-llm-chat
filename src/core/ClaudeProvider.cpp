#include "ClaudeProvider.h"
#include "ToolRegistry.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>

namespace {

QString normalizedReasoningEffort(const QString &effort)
{
    return effort.trimmed().toLower();
}

bool supportsClaudeEffort(const QString &model)
{
    return model.startsWith("claude-opus-4-7")
        || model.startsWith("claude-sonnet-4-7")
        || model.startsWith("claude-haiku-4-7")
        || model.startsWith("claude-opus-4-6")
        || model.startsWith("claude-sonnet-4-6")
        || model.startsWith("claude-opus-4-5");
}

bool usesAdaptiveThinking(const QString &model)
{
    return model.startsWith("claude-opus-4-7")
        || model.startsWith("claude-sonnet-4-7")
        || model.startsWith("claude-opus-4-6")
        || model.startsWith("claude-sonnet-4-6");
}

} // namespace

ClaudeProvider::ClaudeProvider(QNetworkAccessManager *nam,
                               const QString &apiKey,
                               const QString &baseUrl,
                               const QString &model,
                               const QString &reasoningEffort,
                               QObject *parent)
    : LLMProvider(nam, parent)
    , m_apiKey(apiKey)
    , m_baseUrl(baseUrl)
    , m_model(model)
    , m_reasoningEffort(reasoningEffort)
{
}

void ClaudeProvider::doSendStreamingRequest(
    const QList<ChatMessage> &messages,
    const QString &systemPrompt,
    int maxTokens,
    double temperature)
{
    m_accumulatedResponse.clear();
    m_sseParser.reset();
    m_pendingToolCalls.clear();
    m_toolUseBlocks.clear();

    if (m_apiKey.isEmpty()) {
        emit errorOccurred("Claude API key is not configured. Please set it in Settings.");
        return;
    }

    QNetworkRequest request = makeJsonRequest(QUrl(m_baseUrl + "/v1/messages"));
    request.setRawHeader("x-api-key", m_apiKey.toUtf8());
    request.setRawHeader("anthropic-version", "2023-06-01");

    QJsonObject body;
    body["model"] = m_model;
    body["max_tokens"] = maxTokens;
    body["temperature"] = temperature;
    body["stream"] = true;

    const QString reasoningEffort = normalizedReasoningEffort(m_reasoningEffort);
    if (!reasoningEffort.isEmpty()
        && reasoningEffort != "default"
        && supportsClaudeEffort(m_model)) {
        QJsonObject outputConfig;
        outputConfig["effort"] = reasoningEffort;
        body["output_config"] = outputConfig;

        if (usesAdaptiveThinking(m_model)) {
            QJsonObject thinking;
            thinking["type"] = "adaptive";
            body["thinking"] = thinking;
        }
    }

    if (!systemPrompt.isEmpty()) {
        body["system"] = systemPrompt;
    }

    QJsonArray msgArray;

    // Claude 要求 user/assistant 严格交替。但 cancel / 达到工具上限后会话会停在一条
    // user(tool_result)，用户紧接着发的 user 文本（或出错后用户再发一条）就会产生两条
    // 连续同角色消息 → 400 "roles must alternate"。下面把相邻同角色消息的 content 块并进
    // 同一条 turn（user turn 可合法地同时含 tool_result 块和 text 块）。普通交替对话不触发
    // 合并，纯文本消息仍以字符串 content 发出，请求体保持精简。
    auto contentToBlocks = [](const QJsonValue &content) -> QJsonArray {
        if (content.isArray()) return content.toArray();
        QJsonArray arr;
        const QString s = content.toString();
        if (!s.isEmpty()) {
            QJsonObject textBlock;
            textBlock["type"] = "text";
            textBlock["text"] = s;
            arr.append(textBlock);
        }
        return arr;
    };

    for (const auto &msg : messages) {
        if (msg.role == "system") continue;
        QJsonObject m;
        m["role"] = msg.role;

        const bool needsBlocks = !msg.attachments.isEmpty()
                                 || !msg.toolCalls.isEmpty()
                                 || !msg.toolResults.isEmpty();
        if (!needsBlocks) {
            // 纯文本消息：content 为字符串
            m["content"] = msg.content;
        } else {
            // 多模态 / 工具消息：content 为数组，按 Claude 协议拼内容块：
            //   tool_result → {"type":"tool_result","tool_use_id":"...","content":"...","is_error":bool}
            //   image       → {"type":"image","source":{"type":"base64","media_type":"...","data":"..."}}
            //   document    → {"type":"document","source":{...}}
            //   text        → {"type":"text","text":"..."}
            //   tool_use    → {"type":"tool_use","id":"...","name":"...","input":{...}}
            QJsonArray contentArray;

            // tool_result 块放最前：Claude 要求带 tool_use 的 assistant turn 之后的
            // user turn 以 tool_result 打头，tool_use_id 与对应 tool_use 一一对应。
            for (const auto &tr : msg.toolResults) {
                QJsonObject block;
                block["type"] = "tool_result";
                block["tool_use_id"] = tr.toolUseId;
                block["content"] = tr.content;
                if (tr.isError) {
                    block["is_error"] = true;
                }
                contentArray.append(block);
            }

            for (const auto &att : msg.attachments) {
                if (att.type == Attachment::Image) {
                    // 图片：base64 编码发送
                    QJsonObject imageBlock;
                    imageBlock["type"] = "image";
                    QJsonObject source;
                    source["type"] = "base64";
                    source["media_type"] = att.mimeType;
                    source["data"] = QString::fromLatin1(att.fileData.toBase64());
                    imageBlock["source"] = source;
                    contentArray.append(imageBlock);
                } else if (att.type == Attachment::Document) {
                    // PDF 等文档：Claude 原生支持 document 类型
                    QJsonObject docBlock;
                    docBlock["type"] = "document";
                    QJsonObject source;
                    source["type"] = "base64";
                    source["media_type"] = att.mimeType;
                    source["data"] = QString::fromLatin1(att.fileData.toBase64());
                    docBlock["source"] = source;
                    contentArray.append(docBlock);
                }
                // TextFile 由 flattenTextAttachments 统一拼接到下方文本块
            }

            // 文本内容块：文件内容 + 用户输入的消息
            QString fullText = flattenTextAttachments(msg.attachments) + msg.content;
            if (!fullText.trimmed().isEmpty()) {
                QJsonObject textBlock;
                textBlock["type"] = "text";
                textBlock["text"] = fullText;
                contentArray.append(textBlock);
            }

            // tool_use 块放文本之后：assistant turn 先给可选叙述文本，再给工具调用。
            // argsJson 落盘时是字符串，这里 parse 回 input 对象，重放无损。
            for (const auto &tc : msg.toolCalls) {
                QJsonObject block;
                block["type"] = "tool_use";
                block["id"] = tc.id;
                block["name"] = tc.name;
                block["input"] = QJsonDocument::fromJson(tc.argsJson.toUtf8()).object();
                contentArray.append(block);
            }

            m["content"] = contentArray;
        }

        // 与上一条同角色则合并 content 块（维持 user/assistant 交替），否则作为新 turn 追加。
        if (!msgArray.isEmpty()
            && msgArray.last().toObject().value("role").toString() == msg.role) {
            QJsonObject prev = msgArray.last().toObject();
            QJsonArray merged = contentToBlocks(prev.value("content"));
            const QJsonArray addition = contentToBlocks(m.value("content"));
            for (int i = 0; i < addition.size(); ++i) {
                merged.append(addition.at(i));
            }
            prev["content"] = merged;
            msgArray.replace(msgArray.size() - 1, prev);
        } else {
            msgArray.append(m);
        }
    }
    body["messages"] = msgArray;

    // 把已注册的内置工具作为 tools 数组随请求发出，模型据此决定是否发起 tool_use。
    // Claude tools 协议的每个条目形如 {name, description, input_schema}，三个字段
    // 直接取自 Tool 结构。tool_use 块的流式解析在 C3b、执行与回传在 C3c 接入；
    // 这里只负责让模型「看见」工具。无工具注册时不写 tools 字段，保持请求体精简。
    const QList<Tool> tools = ToolRegistry::instance().availableTools();
    if (!tools.isEmpty()) {
        QJsonArray toolsArray;
        for (const auto &tool : tools) {
            QJsonObject def;
            def["name"] = tool.name;
            def["description"] = tool.description;
            def["input_schema"] = tool.inputSchema;
            toolsArray.append(def);
        }
        body["tools"] = toolsArray;
    }

    QNetworkReply *reply = m_nam->post(request, QJsonDocument(body).toJson());
    connectReplySignals(reply);
}

LLMProviderParseResult ClaudeProvider::parseSSEData(const QByteArray &data)
{
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull()) return {};

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    LLMProviderParseResult result;
    if (type == "content_block_start") {
        // tool_use 块起始：记下 id / name，开一个待填充的 ToolCall。
        // 文本 / thinking 块的 start 不带工具信息，忽略即可。
        const QJsonObject block = obj["content_block"].toObject();
        if (block["type"].toString() == "tool_use") {
            ToolCall call;
            call.id = block["id"].toString();
            call.name = block["name"].toString();
            call.argsJson.clear();   // input 由后续 input_json_delta 拼出
            m_toolUseBlocks.insert(obj["index"].toInt(), call);
        }
    } else if (type == "content_block_delta") {
        QJsonObject delta = obj["delta"].toObject();
        const QString deltaType = delta["type"].toString();
        if (deltaType == "text_delta") {
            result.contentToken = delta["text"].toString();
        } else if (deltaType == "thinking_delta") {
            // 扩展思考块的增量内容（adaptive thinking 启用时由模型产出）
            result.reasoningToken = delta["thinking"].toString();
        } else if (deltaType == "input_json_delta") {
            // 工具入参的 JSON 文本增量，按 index 拼到对应 tool_use 块上
            auto it = m_toolUseBlocks.find(obj["index"].toInt());
            if (it != m_toolUseBlocks.end()) {
                it->argsJson += delta["partial_json"].toString();
            }
        }
        // signature_delta 等其他类型：当前不展示，跳过
    } else if (type == "content_block_stop") {
        // content block 收尾：是 tool_use 块就定稿，移入基类 m_pendingToolCalls。
        auto it = m_toolUseBlocks.find(obj["index"].toInt());
        if (it != m_toolUseBlocks.end()) {
            ToolCall call = it.value();
            if (call.argsJson.isEmpty()) {
                call.argsJson = QStringLiteral("{}");   // 无入参工具：补空对象，便于后续 parse
            }
            m_pendingToolCalls.append(call);
            m_toolUseBlocks.erase(it);
        }
    }
    return result;
}
