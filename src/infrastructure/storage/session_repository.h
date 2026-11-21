#pragma once

#include "domain/chat/chat_message.h"

namespace exagent {

class SessionRepository {
public:
    explicit SessionRepository(QString directory);
    QList<ChatSession> load(QString *error) const;
    bool save(const QList<ChatSession> &sessions, QString *error) const;
    QString filePath() const;

private:
    QString m_directory;
};

} // namespace exagent
