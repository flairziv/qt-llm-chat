#include "ToolRegistry.h"

#include <QJsonDocument>
#include <QJsonParseError>

#include <exception>
#include <utility>

ToolRegistry &ToolRegistry::instance()
{
    static ToolRegistry s_instance;
    return s_instance;
}

void ToolRegistry::registerTool(Tool tool)
{
    // 同名替换：先线性查找删掉老的，再 append。工具数量极少（<20），O(n) 查找够用。
    for (int i = 0; i < m_tools.size(); ++i) {
        if (m_tools[i].name == tool.name) {
            m_tools.removeAt(i);
            break;
        }
    }
    m_tools.append(std::move(tool));
}

QList<Tool> ToolRegistry::availableTools() const
{
    return m_tools;
}

QStringList ToolRegistry::toolNames() const
{
    QStringList names;
    names.reserve(m_tools.size());
    for (const auto &t : m_tools) {
        names << t.name;
    }
    return names;
}

const Tool *ToolRegistry::findTool(const QString &name) const
{
    for (const auto &t : m_tools) {
        if (t.name == name) {
            return &t;
        }
    }
    return nullptr;
}

ToolResult ToolRegistry::execute(const QString &name, const QString &argsJson) const
{
    ToolResult result;

    const Tool *tool = findTool(name);
    if (!tool) {
        result.isError = true;
        result.content = QStringLiteral("Unknown tool: %1").arg(name);
        return result;
    }
    if (!tool->execute) {
        result.isError = true;
        result.content = QStringLiteral("Tool has no execute callback: %1").arg(name);
        return result;
    }

    // 空 / 纯空白 argsJson 视为 {}，方便无参数工具
    QJsonObject argsObj;
    const QString trimmed = argsJson.trimmed();
    if (!trimmed.isEmpty()) {
        QJsonParseError parseErr;
        const QJsonDocument doc = QJsonDocument::fromJson(trimmed.toUtf8(), &parseErr);
        if (parseErr.error != QJsonParseError::NoError || !doc.isObject()) {
            result.isError = true;
            result.content = QStringLiteral("Invalid tool arguments JSON for %1: %2")
                                 .arg(name, parseErr.errorString());
            return result;
        }
        argsObj = doc.object();
    }

    // 把工具回调里抛出的 std::exception 也吞掉转成 isError，避免一个写崩的工具
    // 把整个回路炸掉（用户体验：模型干等 + UI 卡在 "Calling tool..."）。
    try {
        return tool->execute(argsObj);
    } catch (const std::exception &e) {
        result.isError = true;
        result.content = QStringLiteral("Tool %1 threw: %2").arg(name, QString::fromUtf8(e.what()));
        return result;
    } catch (...) {
        result.isError = true;
        result.content = QStringLiteral("Tool %1 threw an unknown exception").arg(name);
        return result;
    }
}
