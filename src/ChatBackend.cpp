#include "ChatBackend.h"
#include "core/ClaudeProvider.h"
#include "core/OpenAIProvider.h"
#include <QVariantMap>

ChatBackend::ChatBackend(QObject *parent)
    : QObject(parent)
    , m_settings(new AppSettings(this))
    , m_nam(new QNetworkAccessManager(this))
{
    m_sessionManager = new SessionManager(m_settings->dataDir(), this);

    connect(m_sessionManager, &SessionManager::sessionCreated, this, [this](ChatSession *) {
        emit sessionsChanged();
    });
    connect(m_sessionManager, &SessionManager::sessionDeleted, this, [this](const QString &) {
        emit sessionsChanged();
    });
    connect(m_sessionManager, &SessionManager::activeSessionChanged, this, [this](ChatSession *) {
        emit activeSessionChanged();
        refreshMessages();
    });
    connect(m_settings, &AppSettings::settingsChanged, this, [this]() {
        createProvider();
        emit settingsChanged();
    });

    createProvider();
    m_sessionManager->loadAllSessions();
    if (!m_sessionManager->activeSession())
        m_sessionManager->createSession();
}

void ChatBackend::createProvider()
{
    delete m_provider;
    m_provider = nullptr;
    QString p = m_settings->activeProvider();
    if (p == "claude") {
        auto *cp = new ClaudeProvider(m_nam,
            m_settings->claudeApiKey(),
            m_settings->claudeBaseUrl(),
            m_settings->claudeModel(), this);
        m_provider = cp;
    } else {
        auto *op = new OpenAIProvider(m_nam,
            m_settings->openaiApiKey(),
            m_settings->openaiBaseUrl(),
            m_settings->openaiModel(), this);
        m_provider = op;
    }
    connect(m_provider, &LLMProvider::tokenReceived, this, [this](const QString &token) {
        m_streamingText += token;
        emit streamingTextChanged();
    });
    connect(m_provider, &LLMProvider::responseFinished, this, [this](const QString &full) {
        ChatSession *s = m_sessionManager->activeSession();
        if (s) {
            s->updateLastAssistantMessage(full);
            m_sessionManager->saveSession(s);
        }
        m_streamingText.clear();
        m_responding = false;
        emit respondingChanged();
        emit streamingTextChanged();
        refreshMessages();
    });
    connect(m_provider, &LLMProvider::errorOccurred, this, [this](const QString &err) {
        m_streamingText.clear();
        m_responding = false;
        emit respondingChanged();
        emit streamingTextChanged();
        emit errorOccurred(err);
        refreshMessages();
    });
}

void ChatBackend::refreshMessages()
{
    emit messagesChanged();
}

// --- 属性 getter ---

QVariantList ChatBackend::sessions() const
{
    QVariantList list;
    for (ChatSession *s : m_sessionManager->allSessions()) {
        QVariantMap m;
        m["id"] = s->id();
        m["title"] = s->title();
        m["updatedAt"] = s->updatedAt().toString("yyyy-MM-dd hh:mm");
        list.append(m);
    }
    return list;
}

QVariantList ChatBackend::messages() const
{
    QVariantList list;
    ChatSession *s = m_sessionManager->activeSession();
    if (!s) return list;
    for (const ChatMessage &msg : s->messages()) {
        QVariantMap m;
        m["role"] = msg.role;
        m["content"] = msg.content;
        list.append(m);
    }
    return list;
}

QString ChatBackend::activeSessionId() const
{
    ChatSession *s = m_sessionManager->activeSession();
    return s ? s->id() : QString();
}

bool ChatBackend::responding() const { return m_responding; }
QString ChatBackend::streamingText() const { return m_streamingText; }

QString ChatBackend::activeProvider() const { return m_settings->activeProvider(); }
QString ChatBackend::claudeApiKey() const { return m_settings->claudeApiKey(); }
QString ChatBackend::claudeBaseUrl() const { return m_settings->claudeBaseUrl(); }
QString ChatBackend::claudeModel() const { return m_settings->claudeModel(); }
QString ChatBackend::openaiApiKey() const { return m_settings->openaiApiKey(); }
QString ChatBackend::openaiBaseUrl() const { return m_settings->openaiBaseUrl(); }
QString ChatBackend::openaiModel() const { return m_settings->openaiModel(); }
QString ChatBackend::systemPrompt() const { return m_settings->systemPrompt(); }
int ChatBackend::maxTokens() const { return m_settings->maxTokens(); }
double ChatBackend::temperature() const { return m_settings->temperature(); }

void ChatBackend::setActiveProvider(const QString &v) { m_settings->setActiveProvider(v); }
void ChatBackend::setClaudeApiKey(const QString &v) { m_settings->setClaudeApiKey(v); }
void ChatBackend::setClaudeBaseUrl(const QString &v) { m_settings->setClaudeBaseUrl(v); }
void ChatBackend::setClaudeModel(const QString &v) { m_settings->setClaudeModel(v); }
void ChatBackend::setOpenaiApiKey(const QString &v) { m_settings->setOpenaiApiKey(v); }
void ChatBackend::setOpenaiBaseUrl(const QString &v) { m_settings->setOpenaiBaseUrl(v); }
void ChatBackend::setOpenaiModel(const QString &v) { m_settings->setOpenaiModel(v); }
void ChatBackend::setSystemPrompt(const QString &v) { m_settings->setSystemPrompt(v); }
void ChatBackend::setMaxTokens(int v) { m_settings->setMaxTokens(v); }
void ChatBackend::setTemperature(double v) { m_settings->setTemperature(v); }

// --- 槽函数 ---

void ChatBackend::newSession()
{
    m_sessionManager->createSession();
}

void ChatBackend::selectSession(const QString &id)
{
    m_sessionManager->setActiveSession(id);
}

void ChatBackend::deleteSession(const QString &id)
{
    m_sessionManager->deleteSession(id);
}

void ChatBackend::sendMessage(const QString &text)
{
    if (text.trimmed().isEmpty() || m_responding || !m_provider) return;
    ChatSession *s = m_sessionManager->activeSession();
    if (!s) return;

    s->addMessage({"user", text.trimmed()});
    s->addMessage({"assistant", ""});
    emit sessionsChanged();
    refreshMessages();

    m_responding = true;
    m_streamingText.clear();
    emit respondingChanged();
    emit streamingTextChanged();

    m_provider->sendStreamingRequest(
        s->messages(),
        m_settings->systemPrompt(),
        m_settings->maxTokens(),
        m_settings->temperature()
    );
}

void ChatBackend::stopResponding()
{
    if (m_provider) m_provider->abort();
    m_responding = false;
    m_streamingText.clear();
    emit respondingChanged();
    emit streamingTextChanged();
}
