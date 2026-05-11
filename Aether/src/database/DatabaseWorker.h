#pragma once
#include <QObject>
#include <QSqlDatabase>
#include "models/ContactsModel.h"
#include "models/MessagesModel.h"
#include <QJsonObject>
#include <QTimer>
#include <QSet>

class DatabaseWorker : public QObject {
    Q_OBJECT
public:
    explicit DatabaseWorker(QObject *parent = nullptr);
    ~DatabaseWorker();

public slots:
    // Слот для инициализации БД (будет выполняться в рабочем потоке)
    void initDatabase();
    
    // Слоты для работы с БД
    void loadContacts();
    void addContact(const QString& name, const QString& ip);
    void addFileMessage(int contactId, const QString& localPath);
    
    void loadMessages(int contactId);
    void addMessage(int contactId, const QString& text, bool isMine, int status);
    void clearChat(int contactId);
    void deleteContact(int contactId);
    void renameContact(int contactId, const QString& newName);
    void updateMessageStatus(int messageId, int status);
    void markChatAsRead(int contactId);
    void clearCache();
    
    // Обработка входящих сообщений из сети
    void processIncomingNetworkMessage(const QString& ip, const QString& text, const QString& senderName);
    void processIncomingFileMessage(const QString& ip, const QString& filename, const QByteArray& data, const QString& senderName);
    
    void handlePeerConnected(const QString& ip);
    void handlePeerDisconnected(const QString& ip);
    void handleMessageSendFailed(int messageId);

signals:
    // Сигнал возвращается в UI-поток с результатом
    void databaseInitialized(bool success, const QString& message);
    void contactsLoaded(QList<ContactData> contacts);
    void contactAdded(ContactData contact);
    void messagesLoaded(QList<MessageData> messages);
    void messageAdded(int contactId, MessageData message);
    void contactDeleted(int contactId);
    void messageStatusUpdated(int messageId, int status);
    void contactStatusChanged(int contactId, bool isOnline);
    void contactRenamed(int contactId, const QString& newName);
    void contactMovedToTop(int contactId);
    void contactUnreadCountChanged(int contactId, int count);
    void contactLastMessageChanged(int contactId, const QString& lastMessage);
    void contactOriginalNameChanged(int contactId, const QString& originalName);
    
    // Сигнал для прямой передачи пакета в сетевой воркер
    void requestNetworkSend(int messageId, const QString& ip, const QJsonObject& json);
    
    // Сигнал для попытки подключения (пинг)
    void requestNetworkConnect(const QString& ip, quint16 port);

private slots:
    void processStoreAndForward();
    void processPing();

private:
    QSqlDatabase m_db;
    QTimer* m_snfTimer = nullptr;
    QTimer* m_pingTimer = nullptr;
    QSet<int> m_inFlightMessages; // Хранит ID сообщений, которые ПРЯМО СЕЙЧАС передаются по сети
    
    // Вспомогательный метод создания таблиц
    void createTables();
};