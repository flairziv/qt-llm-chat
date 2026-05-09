#include "AnalyticsPage.h"
#include "core/SessionManager.h"
#include "core/ChatSession.h"
#include <QApplication>
#include <QLabel>
#include <QPalette>
#include <QVBoxLayout>
#include <QShowEvent>
#include <ElaText.h>
#include <ElaTheme.h>

AnalyticsPage::AnalyticsPage(SessionManager *manager, QWidget *parent)
    : QWidget(parent), m_manager(manager)
{
    setupUI();

    // 切主题时刷新一次：QLabel 的 RichText 一旦 setText 后颜色就被解析定型了，
    // 不会随后续 palette 变化自动重算，必须重新 setText 才能让 <b> / 文本颜色更新。
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this]() { refresh(); });
}

void AnalyticsPage::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    ElaText *title = new ElaText("Analytics", this);
    QFont f; f.setPointSize(20); f.setBold(true);
    title->setFont(f);
    layout->addWidget(title);

    m_statsLabel = new QLabel(this);
    m_statsLabel->setTextFormat(Qt::RichText);
    m_statsLabel->setAlignment(Qt::AlignTop);
    m_statsLabel->setWordWrap(true);
    layout->addWidget(m_statsLabel, 1);
}

void AnalyticsPage::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    refresh();
}

/**
 * @brief 遍历所有会话统计数据并生成 HTML 报告
 */
void AnalyticsPage::refresh()
{
    QList<ChatSession*> sessions = m_manager->allSessions();

    int totalSessions = sessions.size();
    int totalMessages = 0;
    int totalChars = 0;
    int claudeSessions = 0;
    int openaiSessions = 0;
    int favoriteMessages = 0;
    ChatSession *mostActive = nullptr;
    int mostActiveCount = 0;

    for (auto *s : sessions) {
        int msgCount = s->messages().size();
        totalMessages += msgCount;
        if (msgCount > mostActiveCount) {
            mostActiveCount = msgCount;
            mostActive = s;
        }
        if (s->providerName() == "claude") ++claudeSessions;
        else if (s->providerName() == "openai") ++openaiSessions;

        for (const auto &msg : s->messages()) {
            totalChars += msg.content.length();
            if (msg.favorite) ++favoriteMessages;
        }
    }
    // 估算 token 数：4 字符 ≈ 1 token
    int estimatedTokens = totalChars / 4;

    // 把当前主题的前景色直接写进 HTML：QLabel 渲染 RichText 时虽然会读 palette，
    // 但 ElaWindow 内部会对子控件 palette 做覆盖，导致夜间模式下数值仍是黑底黑字。
    // 直接 inline color 完全绕过 palette 继承链。
    const QString textColor = qApp->palette().color(QPalette::WindowText).name();

    QString html = QStringLiteral(
        "<table style='border-collapse:collapse; font-size:14px; color:%1;'>"
        "<tr><td style='padding:6px 20px 6px 0;'><b>Total Sessions</b></td><td>%2</td></tr>"
        "<tr><td style='padding:6px 20px 6px 0;'><b>Total Messages</b></td><td>%3</td></tr>"
        "<tr><td style='padding:6px 20px 6px 0;'><b>Estimated Tokens</b></td><td>~%4</td></tr>"
        "<tr><td style='padding:6px 20px 6px 0;'><b>Favorite Messages</b></td><td>%5</td></tr>"
        "<tr><td style='padding:6px 20px 6px 0;'><b>Claude Sessions</b></td><td>%6</td></tr>"
        "<tr><td style='padding:6px 20px 6px 0;'><b>OpenAI Sessions</b></td><td>%7</td></tr>"
        "<tr><td style='padding:6px 20px 6px 0;'><b>Most Active Session</b></td><td>%8 (%9 msgs)</td></tr>"
        "</table>"
    ).arg(textColor)
     .arg(totalSessions)
     .arg(totalMessages)
     .arg(estimatedTokens)
     .arg(favoriteMessages)
     .arg(claudeSessions)
     .arg(openaiSessions)
     .arg(mostActive ? mostActive->title() : "-")
     .arg(mostActiveCount);

    m_statsLabel->setText(html);
}
