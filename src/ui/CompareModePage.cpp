#include "CompareModePage.h"
#include "core/AppSettings.h"
#include "core/ClaudeProvider.h"
#include "core/OpenAIProvider.h"
#include "core/ChatSession.h"
#include <QTextBrowser>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <ElaPushButton.h>
#include <ElaText.h>

CompareModePage::CompareModePage(AppSettings *settings, QNetworkAccessManager *nam, QWidget *parent)
    : QWidget(parent), m_settings(settings), m_nam(nam)
{
    setupUI();
}

void CompareModePage::setupUI()
{
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    ElaText *title = new ElaText("Model Comparison", this);
    QFont f; f.setPointSize(20); f.setBold(true);
    title->setFont(f);
    layout->addWidget(title);

    ElaText *hint = new ElaText("Send the same prompt to both Claude and OpenAI side-by-side.", this);
    hint->setTextPixelSize(12);
    hint->setStyleSheet("color: rgba(128,128,128,0.7);");
    layout->addWidget(hint);

    // 左右分栏显示两个模型的输出
    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);

    QWidget *leftPanel = new QWidget(splitter);
    QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    ElaText *claudeLabel = new ElaText("Claude", leftPanel);
    QFont labelF; labelF.setBold(true);
    claudeLabel->setFont(labelF);
    leftLayout->addWidget(claudeLabel);
    m_claudeOutput = new QTextBrowser(leftPanel);
    m_claudeOutput->setReadOnly(true);
    leftLayout->addWidget(m_claudeOutput);

    QWidget *rightPanel = new QWidget(splitter);
    QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    ElaText *openaiLabel = new ElaText("OpenAI", rightPanel);
    openaiLabel->setFont(labelF);
    rightLayout->addWidget(openaiLabel);
    m_openaiOutput = new QTextBrowser(rightPanel);
    m_openaiOutput->setReadOnly(true);
    rightLayout->addWidget(m_openaiOutput);

    splitter->addWidget(leftPanel);
    splitter->addWidget(rightPanel);
    layout->addWidget(splitter, 1);

    // 输入栏
    QWidget *inputBar = new QWidget(this);
    QHBoxLayout *inputLayout = new QHBoxLayout(inputBar);
    inputLayout->setContentsMargins(0, 0, 0, 0);

    m_inputEdit = new QTextEdit(inputBar);
    m_inputEdit->setPlaceholderText("Enter your prompt...");
    m_inputEdit->setFixedHeight(60);
    inputLayout->addWidget(m_inputEdit);

    m_sendBtn = new ElaPushButton("Send to Both", inputBar);
    m_sendBtn->setFixedSize(120, 60);
    inputLayout->addWidget(m_sendBtn);

    layout->addWidget(inputBar);

    connect(m_sendBtn, &ElaPushButton::clicked, this, &CompareModePage::onSendClicked);
}

/**
 * @brief 点击 Send 时同时向两个 Provider 发起请求
 *
 * 每次对比都创建新的 Provider 实例，流式输出直接写入各自的 QTextBrowser。
 */
void CompareModePage::onSendClicked()
{
    QString prompt = m_inputEdit->toPlainText().trimmed();
    if (prompt.isEmpty()) return;

    m_claudeOutput->clear();
    m_openaiOutput->clear();
    m_claudeOutput->setPlaceholderText("Loading...");
    m_openaiOutput->setPlaceholderText("Loading...");

    // 清理旧的 Provider
    if (m_claudeProvider) { m_claudeProvider->abort(); m_claudeProvider->deleteLater(); }
    if (m_openaiProvider) { m_openaiProvider->abort(); m_openaiProvider->deleteLater(); }

    // 创建两个 Provider
    m_claudeProvider = new ClaudeProvider(m_nam, m_settings->claudeApiKey(),
                                          m_settings->claudeBaseUrl(),
                                          m_settings->claudeModel(),
                                          m_settings->claudeReasoningEffort(), this);
    m_openaiProvider = new OpenAIProvider(m_nam, m_settings->openaiApiKey(),
                                          m_settings->openaiBaseUrl(),
                                          m_settings->openaiModel(),
                                          m_settings->openaiReasoningEffort(), this);

    connect(m_claudeProvider, &LLMProvider::tokenReceived, this, [this](const QString &tok) {
        m_claudeOutput->insertPlainText(tok);
    });
    connect(m_claudeProvider, &LLMProvider::errorOccurred, this, [this](const QString &err) {
        m_claudeOutput->setPlainText("Error: " + err);
    });

    connect(m_openaiProvider, &LLMProvider::tokenReceived, this, [this](const QString &tok) {
        m_openaiOutput->insertPlainText(tok);
    });
    connect(m_openaiProvider, &LLMProvider::errorOccurred, this, [this](const QString &err) {
        m_openaiOutput->setPlainText("Error: " + err);
    });

    QList<ChatMessage> msgs;
    ChatMessage msg;
    msg.role = "user";
    msg.content = prompt;
    msgs.append(msg);

    m_claudeProvider->sendStreamingRequest(msgs, m_settings->systemPrompt(),
                                          m_settings->maxTokens(), m_settings->temperature());
    m_openaiProvider->sendStreamingRequest(msgs, m_settings->systemPrompt(),
                                           m_settings->maxTokens(), m_settings->temperature());
}
