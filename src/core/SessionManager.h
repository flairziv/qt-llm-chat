#pragma once
#include <QObject>
#include <QList>
#include "ChatSession.h"

class SessionManager : public QObject
{
    Q_OBJECT
public:
    explicit SessionManager(const QString &dataDir, QObject *parent = nullptr);

    ChatSession* createSession();
    ChatSession* session(const QString &id) const;
    QList<ChatSession*> allSessions() const;
    void deleteSession(const QString &id);
    void saveSession(ChatSession *session);
    void loadAllSessions();
    ChatSession* activeSession() const;
    void setActiveSession(const QString &id);

signals:
    void sessionCreated(ChatSession *session);
    void sessionDeleted(const QString &id);
    void activeSessionChanged(ChatSession *session);

private:
    QString m_dataDir;
    QList<ChatSession*> m_sessions;
    ChatSession *m_activeSession = nullptr;
    QString sessionFilePath(const QString &id) const;
};
