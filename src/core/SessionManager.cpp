#include "SessionManager.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <algorithm>

SessionManager::SessionManager(const QString &dataDir, QObject *parent)
    : QObject(parent)
    , m_dataDir(dataDir + "/sessions")
{
    QDir dir;
    dir.mkpath(m_dataDir);
}

ChatSession* SessionManager::createSession()
{
    auto *session = new ChatSession(this);
    m_sessions.prepend(session);
    m_activeSession = session;
    emit sessionCreated(session);
    emit activeSessionChanged(session);
    return session;
}

ChatSession* SessionManager::session(const QString &id) const
{
    for (auto *s : m_sessions) {
        if (s->id() == id) return s;
    }
    return nullptr;
}

QList<ChatSession*> SessionManager::allSessions() const
{
    return m_sessions;
}

void SessionManager::deleteSession(const QString &id)
{
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i]->id() == id) {
            ChatSession *s = m_sessions.takeAt(i);
            QFile::remove(sessionFilePath(id));
            if (m_activeSession == s) {
                m_activeSession = m_sessions.isEmpty() ? nullptr : m_sessions.first();
                emit activeSessionChanged(m_activeSession);
            }
            s->deleteLater();
            emit sessionDeleted(id);
            return;
        }
    }
}

void SessionManager::saveSession(ChatSession *session)
{
    if (!session) return;
    QFile file(sessionFilePath(session->id()));
    if (!file.open(QIODevice::WriteOnly)) return;
    file.write(QJsonDocument(session->toJson()).toJson(QJsonDocument::Indented));
}

void SessionManager::loadAllSessions()
{
    QDir dir(m_dataDir);
    const auto entries = dir.entryList({"*.json"}, QDir::Files);
    for (const QString &fname : entries) {
        QFile file(m_dataDir + "/" + fname);
        if (!file.open(QIODevice::ReadOnly)) continue;
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        if (!doc.isObject()) continue;
        ChatSession *s = ChatSession::fromJson(doc.object(), this);
        if (s) m_sessions.append(s);
    }
    std::sort(m_sessions.begin(), m_sessions.end(), [](ChatSession *a, ChatSession *b) {
        return a->updatedAt() > b->updatedAt();
    });
    if (!m_sessions.isEmpty()) {
        m_activeSession = m_sessions.first();
        emit activeSessionChanged(m_activeSession);
    }
    for (auto *s : m_sessions) emit sessionCreated(s);
}

ChatSession* SessionManager::activeSession() const { return m_activeSession; }

void SessionManager::setActiveSession(const QString &id)
{
    ChatSession *s = session(id);
    if (s && s != m_activeSession) {
        m_activeSession = s;
        emit activeSessionChanged(s);
    }
}

QString SessionManager::sessionFilePath(const QString &id) const
{
    return m_dataDir + "/" + id + ".json";
}
