#include "MessagesModel.h"

MessagesModel::MessagesModel(QObject *parent) : QAbstractListModel(parent) {}

int MessagesModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_messages.size();
}

QVariant MessagesModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_messages.size()) return QVariant();

    const MessageData &msg = m_messages[index.row()];
    switch (role) {
        case IdRole: return msg.id;
        case TextRole: return msg.text;
        case IsMineRole: return msg.isMine;
        case StatusRole: return msg.status;
        case TimeRole: return msg.time;
        default: return QVariant();
    }
}

QHash<int, QByteArray> MessagesModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[TextRole] = "text";
    roles[IsMineRole] = "isMine";
    roles[StatusRole] = "status";
    roles[TimeRole] = "time";
    return roles;
}

void MessagesModel::setMessages(const QList<MessageData>& messages) {
    beginResetModel();
    m_messages = messages;
    endResetModel();
}

void MessagesModel::appendMessage(const MessageData& message) {
    beginInsertRows(QModelIndex(), m_messages.size(), m_messages.size());
    m_messages.append(message);
    endInsertRows();
}

void MessagesModel::clear() {
    beginResetModel();
    m_messages.clear();
    endResetModel();
}

void MessagesModel::updateMessageStatus(int messageId, int status) {
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages[i].id == messageId) {
            m_messages[i].status = status;
            QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx, {StatusRole});
            break;
        }
    }
}