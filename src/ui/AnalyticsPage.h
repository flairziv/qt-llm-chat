#pragma once
#include <QWidget>

class QLabel;
class SessionManager;

/**
 * @brief 会话分析页 —— 统计所有会话的数据
 *
 * 显示：总会话数、总消息数、估算 token 总数、最活跃会话、按 Provider 分布。
 * 每次导航到该页面时自动刷新统计。
 */
class AnalyticsPage : public QWidget
{
    Q_OBJECT
public:
    explicit AnalyticsPage(SessionManager *manager, QWidget *parent = nullptr);

    /** @brief 刷新统计数据（切换到该页面或数据变化时调用） */
    void refresh();

protected:
    void showEvent(QShowEvent *event) override;

private:
    void setupUI();

    SessionManager *m_manager;
    QLabel *m_statsLabel;       // 统计数据的 HTML 表格
};
