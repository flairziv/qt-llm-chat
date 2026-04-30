#pragma once
#include <QWidget>

class QTextBrowser;
class QTextEdit;
class ElaPushButton;
class AppSettings;
class LLMProvider;
class QNetworkAccessManager;

/**
 * @brief 模型对比页 —— 同时向 Claude 和 OpenAI 发送同一个问题并并排显示结果
 *
 * 简化版：不接入会话管理，每次对比都是独立请求。
 */
class CompareModePage : public QWidget
{
    Q_OBJECT
public:
    explicit CompareModePage(AppSettings *settings, QNetworkAccessManager *nam, QWidget *parent = nullptr);

private slots:
    void onSendClicked();

private:
    void setupUI();

    AppSettings *m_settings;
    QNetworkAccessManager *m_nam;
    LLMProvider *m_claudeProvider = nullptr;
    LLMProvider *m_openaiProvider = nullptr;

    QTextEdit *m_inputEdit;
    ElaPushButton *m_sendBtn;
    QTextBrowser *m_claudeOutput;
    QTextBrowser *m_openaiOutput;
};
