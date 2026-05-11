#pragma once
#include <QObject>
#include <QThread>
#include "database/DatabaseWorker.h"
#include "network/NetworkWorker.h"
#include "models/ContactsModel.h"
#include "models/MessagesModel.h"
#include <QJsonObject>
#include <QUrl>
#include <QSystemTrayIcon>

class AppCore : public QObject {
    Q_OBJECT
    // Экспортируем модель в QML
    Q_PROPERTY(ContactsModel* contactsModel READ contactsModel CONSTANT)
    Q_PROPERTY(MessagesModel* messagesModel READ messagesModel CONSTANT)
public:
    explicit AppCore(QObject *parent = nullptr);
    ~AppCore();

    void initialize();
    ContactsModel* contactsModel() const { return m_contactsModel; }
    MessagesModel* messagesModel() const { return m_messagesModel; }

    // Метод, который мы сможем вызывать из QML (JS)
    Q_INVOKABLE void requestAddContact(const QString& name, const QString& ip);
    Q_INVOKABLE void requestLoadMessages(int contactId);
    Q_INVOKABLE void requestSendMessage(int contactId, const QString& text);
    Q_INVOKABLE void requestClearChat(int contactId);
    Q_INVOKABLE void requestDeleteContact(int contactId);
    Q_INVOKABLE void requestRenameContact(int contactId, const QString& newName);
    Q_INVOKABLE void requestMarkChatAsRead(int contactId);
    Q_INVOKABLE void requestSendFile(int contactId, const QUrl& fileUrl);
    Q_INVOKABLE bool requestPasteFromClipboard(int contactId);
    Q_INVOKABLE void requestClearCache();
    Q_INVOKABLE QString getLocalIpAddress();

signals:
    void startDbInit();
    void startNetworkInit(quint16 port);
    
    void sendJsonToNetwork(int messageId, const QString& ip, const QJsonObject& json);

    void requestLoadContacts();
    void requestAddContactToDb(const QString& name, const QString& ip);
    void requestLoadMessagesFromDb(int contactId);
    void requestAddMessageToDb(int contactId, const QString& text, bool isMine, int status);
    void requestClearChatInDb(int contactId);
    void requestDeleteContactInDb(int contactId);
    void requestRenameContactInDb(int contactId, const QString& newName);
    void requestProcessIncomingNetworkMessage(const QString& ip, const QString& text, const QString& senderName);
    void requestMarkChatAsReadInDb(int contactId);
    void requestAddFileMessageToDb(int contactId, const QString& localPath);
    void requestProcessIncomingFileMessage(const QString& ip, const QString& filename, const QByteArray& data, const QString& senderName);
    void requestClearCacheInDb();
    
    void originalNameUpdated(int contactId, const QString& originalName);

private slots:
    void onDbInitialized(bool success, const QString& message);
    void onNetworkStarted(bool success, const QString& message);
    void onNetworkMessageReceived(const QString& ip, const QJsonObject& json);
    void onMessageAdded(int contactId, const MessageData& message);
    void onMessageUploadProgress(int messageId, double progress);

private:
    QThread m_dbThread;
    DatabaseWorker* m_dbWorker;

    QThread m_networkThread;
    NetworkWorker* m_networkWorker;
    
    ContactsModel* m_contactsModel;
    MessagesModel* m_messagesModel;

    int m_currentContactId = -1;
    
    QSystemTrayIcon* m_trayIcon = nullptr;
};