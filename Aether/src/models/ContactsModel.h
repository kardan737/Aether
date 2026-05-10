#pragma once
#include <QAbstractListModel>
#include <QString>
#include <QList>
#include <QMetaType>

// Структура для хранения данных контакта
struct ContactData {
    int id;
    QString name;
    bool isOnline;
    double decayLevel;
    int unreadCount;
    QString lastMessage;
};
Q_DECLARE_METATYPE(ContactData)
Q_DECLARE_METATYPE(QList<ContactData>)

class ContactsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        IsOnlineRole,
        DecayLevelRole,
        UnreadCountRole,
        LastMessageRole
    };

    explicit ContactsModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

public slots:
    // Слоты для получения данных из DatabaseWorker
    void setContacts(const QList<ContactData>& contacts);
    void appendContact(const ContactData& contact);
    void removeContact(int contactId);
    void updateContactStatus(int contactId, bool isOnline);
    void updateContactName(int contactId, const QString& newName);
    void moveContactToTop(int contactId);
    void updateContactUnreadCount(int contactId, int count);
    void updateContactLastMessage(int contactId, const QString& lastMessage);

private:
    QList<ContactData> m_contacts;
};