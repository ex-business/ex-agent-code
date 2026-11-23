#include "model_catalog.h"
#include <QDir>
#include <QJsonArray>
namespace exagent::codex {
ModelCatalog::ModelCatalog(Account account, QObject *parent)
    : QObject(parent)
    , m_account(account)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(20000);
    connect(&m_timeout, &QTimer::timeout, this, [this] { emit failed(QStringLiteral("查询模型超时。")); });
    connect(&m_connection, &Connection::failure, this, &ModelCatalog::failed);
    connect(&m_connection, &Connection::ready, this, [this] {
        m_connection.send({ { "id", ++m_id }, { "method", "initialize" },
            { "params",
                QJsonObject {
                    { "clientInfo", QJsonObject { { "name", "ex_agent_code" }, { "version", "0.1" } } } } } });
    });
    connect(&m_connection, &Connection::frame, this, [this](const QJsonObject &frame) {
        if (frame["id"].toInt(-1) != m_id || frame.contains("method"))
            return;
        if (frame.contains("error")) {
            emit failed(QStringLiteral("Codex 无法查询模型目录。"));
            return;
        }
        if (m_id == 1) {
            m_connection.send({ { "method", "initialized" } });
            page({});
            return;
        }
        const auto result = frame["result"].toObject();
        for (const auto &entry : result["data"].toArray()) {
            const auto model = entry.toObject()["model"].toString();
            if (!model.isEmpty() && !m_models.contains(model))
                m_models.append(model);
        }
        const auto cursor = result["nextCursor"].toString();
        if (cursor.isEmpty()) {
            m_timeout.stop();
            emit loaded(m_models);
        } else if (m_cursors.contains(cursor) || m_cursors.size() >= 100)
            emit failed(QStringLiteral("模型分页游标无效。"));
        else
            page(cursor);
    });
}
void ModelCatalog::page(const QString &cursor)
{
    QJsonObject params { { "limit", 100 } };
    if (!cursor.isEmpty()) {
        m_cursors.insert(cursor);
        params.insert("cursor", cursor);
    }
    m_connection.send({ { "id", ++m_id }, { "method", "model/list" }, { "params", params } });
}
void ModelCatalog::start()
{
    m_timeout.start();
    m_connection.start(m_account, QDir::homePath());
}
}
