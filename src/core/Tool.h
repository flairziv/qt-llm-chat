#pragma once
#include <QJsonObject>
#include <QString>

#include <functional>

/**
 * @brief 工具的默认风险级别
 *
 * 决定 MainWindow 在执行工具前是否弹审批对话框（C6 才接 UI；C1 阶段所有工具
 * 都按 ReadOnly 执行，直接跑通协议链路）：
 *
 *   - ReadOnly        不弹窗，直接执行（read_file / list_directory 等）
 *   - Mutating        每次弹审批（write_file / take_screenshot 等）
 *   - ShellOrNetwork  弹审批 + "Allow for this session" 选项
 *                     （execute_powershell / fetch_url 等）
 *
 * 注册时声明的级别会被设置页（C7）的 risk override 覆盖，所以这里只是默认值。
 */
enum class RiskLevel {
    ReadOnly,
    Mutating,
    ShellOrNetwork
};

/**
 * @brief RiskLevel ↔ 字符串互转
 *
 * 用于 settings.ini 里人读的 risk override 字段（设置页 C7 写、审批门读）。
 * 存字符串而非数字，让用户直接看 INI 就能懂 / 手改。未知字符串回退到 fallback，
 * 向后兼容（删除某个级别名 / 手写错时不会崩）。
 */
inline QString riskLevelToString(RiskLevel level)
{
    switch (level) {
    case RiskLevel::ReadOnly:       return QStringLiteral("ReadOnly");
    case RiskLevel::Mutating:       return QStringLiteral("Mutating");
    case RiskLevel::ShellOrNetwork: return QStringLiteral("ShellOrNetwork");
    }
    return QStringLiteral("ReadOnly");
}

inline RiskLevel riskLevelFromString(const QString &s, RiskLevel fallback = RiskLevel::ReadOnly)
{
    if (s == QLatin1String("ReadOnly"))       return RiskLevel::ReadOnly;
    if (s == QLatin1String("Mutating"))       return RiskLevel::Mutating;
    if (s == QLatin1String("ShellOrNetwork")) return RiskLevel::ShellOrNetwork;
    return fallback;
}

/**
 * @brief 一次工具执行的结果
 *
 * toolUseId 关联到 ToolCall::id，由 MainWindow 在拿到 ToolRegistry::execute()
 * 返回值后回填——工具实现本身不知道自己被哪一次 tool_use 调用。
 *
 * isError=true 时 content 是人类可读的错误说明。错误结果仍然会回填给模型
 * （tool_result.is_error=true），让模型自己决定要不要换条路再试，而不是
 * 应用层吞掉错误后模型干等。
 */
struct ToolResult {
    QString toolUseId;
    QString content;
    bool isError = false;

    /**
     * @brief 序列化为 session JSON 中的 tool_result 块
     *
     * 键名沿用 Claude 协议的 snake_case（tool_use_id / is_error），人读 session
     * 文件时能直接对应 API 文档。is_error 仅在为真时写出——与 favorite 字段
     * 同一风格：默认值不落盘，保持 JSON 精简。
     */
    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj["tool_use_id"] = toolUseId;
        obj["content"] = content;
        if (isError) {
            obj["is_error"] = true;
        }
        return obj;
    }

    /** @brief 从 session JSON 的 tool_result 块还原；缺字段走默认值，向后兼容 */
    static ToolResult fromJson(const QJsonObject &obj)
    {
        ToolResult r;
        r.toolUseId = obj["tool_use_id"].toString();
        r.content = obj["content"].toString();
        r.isError = obj["is_error"].toBool(false);
        return r;
    }
};

/**
 * @brief 一次工具调用的描述（来自 LLM 的 tool_use 块）
 *
 * Claude 协议：id 形如 "toolu_01abc..."，argsJson 是 input_json_delta 流式拼接
 * 出的完整 JSON 字符串。C3 commit 在 ClaudeProvider::parseSSEData 里逐 delta
 * 累积，content_block_stop 时把完整 ToolCall 入队。
 */
struct ToolCall {
    QString id;
    QString name;
    QString argsJson;   // 完整 JSON 对象字符串（content_block_stop 后才完整）

    /**
     * @brief 序列化为 session JSON 中的 tool_use 块
     *
     * argsJson 原样写成字符串字段 "args"，不在此 parse 成对象——ToolCall 全程
     * 以 JSON 字符串为规范形态，C3 拼 Claude 请求体时才 parse 成 input 对象，
     * 保证落盘 / 重放无损。
     */
    QJsonObject toJson() const
    {
        QJsonObject obj;
        obj["id"] = id;
        obj["name"] = name;
        obj["args"] = argsJson;
        return obj;
    }

    /** @brief 从 session JSON 的 tool_use 块还原 */
    static ToolCall fromJson(const QJsonObject &obj)
    {
        ToolCall c;
        c.id = obj["id"].toString();
        c.name = obj["name"].toString();
        c.argsJson = obj["args"].toString();
        return c;
    }
};

/**
 * @brief 注册到 ToolRegistry 的工具元数据 + 执行回调
 *
 * Provider 用 name / description / inputSchema 拼请求体里的 tools 数组；
 * MainWindow 用 riskLevel 决定审批策略；execute 在审批通过后由 ToolRegistry
 * 调用（args 已经从 JSON 字符串 parse 成 QJsonObject）。
 */
struct Tool {
    /** @brief 工具名（snake_case，作为 LLM tool_use.name 的取值） */
    QString name;

    /** @brief 给 LLM 看的描述：告诉它什么时候用这个工具 */
    QString description;

    /**
     * @brief Claude tools 协议要求的 input_schema（JSON Schema 子集）
     *
     * 形如：
     *   {
     *     "type": "object",
     *     "properties": { "path": {"type":"string","description":"..."} },
     *     "required": ["path"]
     *   }
     */
    QJsonObject inputSchema;

    /** @brief 默认风险级别（可被设置页覆盖） */
    RiskLevel riskLevel = RiskLevel::ReadOnly;

    /**
     * @brief 执行回调
     *
     * ToolRegistry::execute 负责把 argsJson 字符串 parse 成 QJsonObject 后再调，
     * 工具实现不必关心 JSON 解析失败的情形。返回 ToolResult 的 toolUseId 字段
     * 留空，由 MainWindow 关联到对应 ToolCall::id。
     */
    std::function<ToolResult(const QJsonObject &args)> execute;
};
