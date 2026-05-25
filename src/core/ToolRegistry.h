#pragma once
#include "Tool.h"

#include <QList>
#include <QString>
#include <QStringList>

/**
 * @brief 内置 LLM 工具的全局注册表（单例）
 *
 * 由 main.cpp 启动阶段调用各 registerXxxTools() 自由函数填充，运行期是只读的
 * （C7 加配置过滤时也只是在调用端 filter 副本，不改 registry 本身）。
 *
 * 提供两类访问：
 *   - 给 Provider 用：availableTools() → 拼请求体里的 tools 数组
 *   - 给 MainWindow 用：execute(name, args) → 路由到对应工具的回调
 *
 * 不是线程安全的：所有调用都在 Qt 主线程（GUI 线程）。C8 异步化时把执行移到
 * 工作线程后，需要重新审视——但元数据查询（availableTools / findTool）仍然
 * 只在主线程发生。
 */
class ToolRegistry
{
public:
    /** @brief 进程内单例访问 */
    static ToolRegistry &instance();

    /** @brief 注册一个工具；同名工具会被替换（用于热替换 / 测试覆盖） */
    void registerTool(Tool tool);

    /** @brief 已注册的所有工具（顺序为注册顺序） */
    QList<Tool> availableTools() const;

    /** @brief 工具名清单（用于设置页 / 启动日志） */
    QStringList toolNames() const;

    /** @brief 按名查工具，未找到返回 nullptr */
    const Tool *findTool(const QString &name) const;

    /**
     * @brief 执行一次工具调用
     *
     * 统一负责：参数 JSON 字符串 → QJsonObject 解析、调用 execute 回调、
     * 兜底处理（未知工具 / JSON 解析失败 / 回调未设置 / 回调抛异常）。
     * 返回的 ToolResult.toolUseId 字段为空，由调用方填入。
     */
    ToolResult execute(const QString &name, const QString &argsJson) const;

private:
    ToolRegistry() = default;

    QList<Tool> m_tools;
};
