#pragma once
#include <QWidget>

class AppSettings;

/**
 * @brief 工具调用设置页（C7c）
 *
 * Settings 分组下的一页，把 C7a 的工具设置暴露给用户、免去手改 settings.ini：
 *   - 总开关 Enable tools（toolsEnabled）：关掉后请求不带 tools 数组，模型不发起调用，
 *     且整组工具行变灰禁用。
 *   - 每个已注册工具（ToolRegistry::availableTools()）一行：启用开关
 *     （toolEnabled(name)）+ 风险级别下拉（toolRiskOverride(name)，Default = 用注册默认）。
 *
 * 全部即时写回 AppSettings；这些设置无信号，由 ClaudeProvider / 审批门在请求时实时读
 * （见 C7a/C7b），改完下一条消息即生效，不重建 Provider。纯代码构建（工具行数由注册表
 * 动态决定），不用 .ui。
 */
class SettingToolsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingToolsPage(AppSettings *settings, QWidget *parent = nullptr);

private:
    void setupUI();

    AppSettings *m_settings;
    QWidget *m_toolsContainer = nullptr;  // 所有工具行的容器；总开关关闭时整体禁用
};
