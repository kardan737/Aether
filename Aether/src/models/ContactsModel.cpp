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
        default: return QVariant();
    }
}

QHash<int, QByteArray> ContactsModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[IdRole] = "id";
    roles[NameRole] = "name";
    roles[IsOnlineRole] = "isOnline";
    roles[DecayLevelRole] = "decayLevel";
    return roles;
}

void ContactsModel::setContacts(const QList<ContactData>& contacts) {
    beginResetModel();
    m_contacts = contacts;
    endResetModel();
}

void ContactsModel::appendContact(const ContactData& contact) {
    // Уведомляем QML, что добавилась 1 строка в конец
    beginInsertRows(QModelIndex(), m_contacts.size(), m_contacts.size());
    m_contacts.append(contact);
    endInsertRows();
}