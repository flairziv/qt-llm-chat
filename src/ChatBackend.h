#pragma once
#include <QObject>
#include <QNetworkAccessManager>
#include <QVariantList>
#include <QVariantMap>
#include "core/AppSettings.h"
#include "core/SessionManager.h"
#include "core/LLMProvider.h"

/**
 * @brief QML 与 C++ 后端的桥接类
 *
 * 暴露给 QML 的属性和方法，负责协调 AppSettings、SessionManager、LLMProvider。
 */
class ChatBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList sessions READ sessions NOTIFY sessionsChanged)
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)
    Q_PROPERTY(QString activeSessionId READ activeSessionId NOTIFY activeSessionChanged)
    Q_PROPERTY(bool responding READ responding NOTIFY respondingChanged)
    Q_PROPERTY(QString streamingText READ streamingText NOTIFY streamingTextChanged)

    // 设置属性
    Q_PROPERTY(QString activeProvider READ activeProvider WRITE setActiveProvider NOTIFY settingsChanged)
    Q_PROPERTY(QString claudeApiKey READ claudeApiKey WRITE setClaudeApiKey NOTIFY settingsChanged)
    Q_PROPERTY(QString claudeBaseUrl READ claudeBaseUrl WRITE setClaudeBaseUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString claudeModel READ claudeModel WRITE setClaudeModel NOTIFY settingsChanged)
    Q_PROPERTY(QString openaiApiKey READ openaiApiKey WRITE setOpenaiApiKey NOTIFY settingsChanged)
    Q_PROPERTY(QString openaiBaseUrl READ openaiBaseUrl WRITE setOpenaiBaseUrl NOTIFY settingsChanged)
    Q_PROPERTY(QString openaiModel READ openaiModel WRITE setOpenaiModel NOTIFY settingsChanged)
    Q_PROPERTY(QString systemPrompt READ systemPrompt WRITE setSystemPrompt NOTIFY settingsChanged)
    Q_PROPERTY(int maxTokens READ maxTokens WRITE setMaxTokens NOTIFY settingsChanged)
    Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY settingsChanged)

public:
    explicit ChatBackend(QObject *parent = nullptr);

    // --- 会话数据 ---
    QVariantList sessions() const;
    QVariantList messages() const;
    QString activeSessionId() const;
    bool responding() const;
    QString streamingText() const;

    // --- 设置 getter ---
    QString activeProvider() const;
    QString claudeApiKey() const;
    QString claudeBaseUrl() const;
    QString claudeModel() const;
    QString openaiApiKey() const;
    QString openaiBaseUrl() const;
    QString openaiModel() const;
    QString systemPrompt() const;
    int maxTokens() const;
    double temperature() const;

    // --- 设置 setter ---
    void setActiveProvider(const QString &v);
    void setClaudeApiKey(const QString &v);
    void setClaudeBaseUrl(const QString &v);
    void setClaudeModel(const QString &v);
    void setOpenaiApiKey(const QString &v);
    void setOpenaiBaseUrl(const QString &v);
    void setOpenaiModel(const QString &v);
    void setSystemPrompt(const QString &v);
    void setMaxTokens(int v);
    void setTemperature(double v);

public slots:
    void newSession();
    void selectSession(const QString &id);
    void deleteSession(const QString &id);
    void sendMessage(const QString &text);
    void stopResponding();

signals:
    void sessionsChanged();
    void messagesChanged();
    void activeSessionChanged();
    void respondingChanged();
    void streamingTextChanged();
    void settingsChanged();
    void errorOccurred(const QString &message);

private:
    void createProvider();
    void refreshMessages();

    AppSettings *m_settings;
    SessionManager *m_sessionManager;
    QNetworkAccessManager *m_nam;
    LLMProvider *m_provider = nullptr;

    bool m_responding = false;
    QString m_streamingText;
};
