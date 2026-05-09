#include "ContactsModel.h"

ContactsModel::ContactsModel(QObject *parent) : QAbstractListModel(parent) {}

int ContactsModel::rowCount(const QModelIndex &parent) const {
    if (parent.isValid()) return 0;
    return m_contacts.size();
}

QVariant ContactsModel::data(const QModelIndex &index, int role) const {
    if (!index.isValid() || index.row() >= m_contacts.size()) return QVariant();

    const ContactData &contact = m_contacts[index.row()];
    switch (role) {
        case IdRole: return contact.id;
        case NameRole: return contact.name;
        case IsOnlineRole: return contact.isOnline;
        case DecayLevelRole: return contact.decayLevel;
        case UnreadCountRole: return contact.unreadCount;
        default: return QVariant();
    }
}

QHash<int, QByteArray> ContactsModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[NameRole] = "name";
    roles[IsOnlineRole] = "isOnline";
    roles[DecayLevelRole] = "decayLevel";
    roles[UnreadCountRole] = "unreadCount";
    return roles;
}

void ContactsModel::setContacts(const QList<ContactData>& contacts) {
    beginResetModel();
    m_contacts = contacts;
    endResetModel();
}

void ContactsModel::appendContact(const ContactData& contact) {
    // Уведомляем QML, что добавилась 1 строка в начало (наверх)
    beginInsertRows(QModelIndex(), 0, 0);
    m_contacts.prepend(contact);
    endInsertRows();
}

void ContactsModel::removeContact(int contactId) {
    for (int i = 0; i < m_contacts.size(); ++i) {
        if (m_contacts[i].id == contactId) {
            beginRemoveRows(QModelIndex(), i, i);
            m_contacts.removeAt(i);
            endRemoveRows();
            break;
        }
    }
}

void ContactsModel::updateContactUnreadCount(int contactId, int count) {
    for (int i = 0; i < m_contacts.size(); ++i) {
        if (m_contacts[i].id == contactId) {
            m_contacts[i].unreadCount = count;
            QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx, {UnreadCountRole});
            break;
        }
    }
}

void ContactsModel::moveContactToTop(int contactId) {
    for (int i = 0; i < m_contacts.size(); ++i) {
        if (m_contacts[i].id == contactId) {
            if (i == 0) return; // Уже на первом месте
            
            beginMoveRows(QModelIndex(), i, i, QModelIndex(), 0);
            m_contacts.move(i, 0);
            endMoveRows();
            break;
        }
    }
}

void ContactsModel::updateContactStatus(int contactId, bool isOnline) {
    for (int i = 0; i < m_contacts.size(); ++i) {
        if (m_contacts[i].id == contactId) {
            m_contacts[i].isOnline = isOnline;
            QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx, {IsOnlineRole});
            break;
        }
    }
}

void ContactsModel::updateContactName(int contactId, const QString& newName) {
    for (int i = 0; i < m_contacts.size(); ++i) {
        if (m_contacts[i].id == contactId) {
            m_contacts[i].name = newName;
            QModelIndex idx = index(i, 0);
            emit dataChanged(idx, idx, {NameRole});
            break;
        }
    }
}