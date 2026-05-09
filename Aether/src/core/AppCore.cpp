#include "AppCore.h"
#include <QDebug>
#include <QJsonObject>
#include <QHostAddress>
#include <QNetworkInterface>

AppCore::AppCore(QObject *parent) : QObject(parent) {
    // Обязательная регистрация типов для работы через Qt::QueuedConnection между потоками
    qRegisterMetaType<ContactData>("ContactData");
    qRegisterMetaType<QList<ContactData>>("QList<ContactData>");
    qRegisterMetaType<MessageData>("MessageData");
    qRegisterMetaType<QList<MessageData>>("QList<MessageData>");

    m_contactsModel = new ContactsModel(this);
    m_messagesModel = new MessagesModel(this);
    m_dbWorker = new DatabaseWorker();
    m_networkWorker = new NetworkWorker();
    
    m_dbWorker->moveToThread(&m_dbThread);
    m_networkWorker->moveToThread(&m_networkThread);

    connect(&m_dbThread, &QThread::finished, m_dbWorker, &QObject::deleteLater);
    connect(&m_networkThread, &QThread::finished, m_networkWorker, &QObject::deleteLater);

    // Связи инициализации
    connect(this, &AppCore::startDbInit, m_dbWorker, &DatabaseWorker::initDatabase);
    connect(m_dbWorker, &DatabaseWorker::databaseInitialized, this, &AppCore::onDbInitialized);
    
    // Связи сети
    connect(this, &AppCore::startNetworkInit, m_networkWorker, &NetworkWorker::startServer);
    connect(m_networkWorker, &NetworkWorker::serverStarted, this, &AppCore::onNetworkStarted);
    
    connect(m_networkWorker, &NetworkWorker::messageReceived, this, &AppCore::onNetworkMessageReceived);
    connect(m_networkWorker, &NetworkWorker::peerConnected, m_dbWorker, &DatabaseWorker::handlePeerConnected);
    connect(m_networkWorker, &NetworkWorker::peerDisconnected, m_dbWorker, &DatabaseWorker::handlePeerDisconnected);

    connect(this, &AppCore::sendJsonToNetwork, m_networkWorker, &NetworkWorker::sendJsonMessage);

    // Прямая связь: БД напрямую просит Сеть отправить пакет (работает через QueuedConnection)
    connect(m_dbWorker, &DatabaseWorker::requestNetworkSend, m_networkWorker, &NetworkWorker::sendJsonMessage);
    connect(m_dbWorker, &DatabaseWorker::requestNetworkConnect, m_networkWorker, &NetworkWorker::connectToPeer);
    connect(this, &AppCore::requestProcessIncomingNetworkMessage, m_dbWorker, &DatabaseWorker::processIncomingNetworkMessage);

    // Связи работы с контактами
    connect(this, &AppCore::requestLoadContacts, m_dbWorker, &DatabaseWorker::loadContacts);
    connect(this, &AppCore::requestAddContactToDb, m_dbWorker, &DatabaseWorker::addContact);

    connect(m_dbWorker, &DatabaseWorker::contactsLoaded, m_contactsModel, &ContactsModel::setContacts);
    connect(m_dbWorker, &DatabaseWorker::contactAdded, m_contactsModel, &ContactsModel::appendContact);

    // Связи работы с сообщениями
    connect(this, &AppCore::requestLoadMessagesFromDb, m_dbWorker, &DatabaseWorker::loadMessages);
    connect(this, &AppCore::requestAddMessageToDb, m_dbWorker, &DatabaseWorker::addMessage);
    connect(this, &AppCore::requestClearChatInDb, m_dbWorker, &DatabaseWorker::clearChat);
    connect(this, &AppCore::requestDeleteContactInDb, m_dbWorker, &DatabaseWorker::deleteContact);

    connect(m_dbWorker, &DatabaseWorker::messagesLoaded, m_messagesModel, &MessagesModel::setMessages);
    connect(m_dbWorker, &DatabaseWorker::messageAdded, m_messagesModel, &MessagesModel::appendMessage);
    connect(m_dbWorker, &DatabaseWorker::contactDeleted, m_contactsModel, &ContactsModel::removeContact);

    connect(m_dbWorker, &DatabaseWorker::messageStatusUpdated, m_messagesModel, &MessagesModel::updateMessageStatus);
    connect(m_dbWorker, &DatabaseWorker::contactStatusChanged, m_contactsModel, &ContactsModel::updateContactStatus);

    m_dbThread.start();
    m_networkThread.start();
}

AppCore::~AppCore() {
    m_dbThread.quit();
    m_dbThread.wait();
    
    m_networkThread.quit();
    m_networkThread.wait();
}

void AppCore::initialize() {
    emit startDbInit();
}

void AppCore::onDbInitialized(bool success, const QString& message) {
    qDebug() << "Aether Backend:" << message;
    if (success) {
        emit requestLoadContacts(); // После запуска БД сразу просим загрузить контакты
        
        // Когда БД загружена, запускаем P2P-сервер (например, на порту 7777)
        emit startNetworkInit(7777);
    }
}

void AppCore::onNetworkStarted(bool success, const QString& message) {
    qDebug() << "Aether Network:" << message;
}

void AppCore::requestAddContact(const QString& name, const QString& ip) {
    QHostAddress targetAddress(ip);
    
    // 1. Запрещаем добавление петлевых адресов (127.0.0.1, ::1) и нулей
    if (targetAddress.isLoopback() || ip == "0.0.0.0") {
        qWarning() << "Aether: Cannot add loopback/localhost as a contact!";
        return;
    }
    
    // 2. Запрещаем добавление собственных IP-адресов этого компьютера (LAN, VPN)
    const QList<QHostAddress> localAddresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : localAddresses) {
        if (address.toString() == ip) {
            qWarning() << "Aether: Cannot add your own local IP address!";
            return;
        }
    }

    emit requestAddContactToDb(name, ip);
    
    // Сразу пытаемся открыть P2P-туннель по указанному IP
    QMetaObject::invokeMethod(m_networkWorker, "connectToPeer", Qt::QueuedConnection, 
                              Q_ARG(QString, ip), Q_ARG(quint16, 7777));
}

void AppCore::requestLoadMessages(int contactId) {
    emit requestLoadMessagesFromDb(contactId);
}

void AppCore::requestSendMessage(int contactId, const QString& text) {
    emit requestAddMessageToDb(contactId, text, true, 0); // isMine=true, status=0 (ожидает)
    // Отправка в сеть теперь автоматически произойдет внутри DatabaseWorker::addMessage
}

void AppCore::requestClearChat(int contactId) {
    emit requestClearChatInDb(contactId);
    m_messagesModel->clear(); // Очищаем и на фронтенде тоже
}

void AppCore::requestDeleteContact(int contactId) {
    emit requestDeleteContactInDb(contactId);
    m_messagesModel->clear(); // Очищаем сообщения на экране, так как чат удален
}

void AppCore::onNetworkMessageReceived(const QString& ip, const QJsonObject& json) {
    QString type = json["type"].toString();
    if (type == "message") {
        QString text = json["text"].toString();
        QString senderName = json.value("sender_name").toString(); // Достаем имя друга
        
        qDebug() << "Aether P2P: Received message from" << (senderName.isEmpty() ? ip : senderName) << ":" << text;
        
        // Просим базу данных разобраться, чей это IP, и сохранить текст
        emit requestProcessIncomingNetworkMessage(ip, text, senderName);
        
        // Отправляем "истинную галочку" (ACK) обратно собеседнику
        if (json.contains("msg_id")) {
            QJsonObject ack;
            ack["type"] = "ack";
            ack["msg_id"] = json["msg_id"].toInt();
            emit sendJsonToNetwork(0, ip, ack); // ID 0, так как само подтверждение не нужно отслеживать
        }
    } else if (type == "ack") {
        // Собеседник подтвердил получение! Теперь ставим галочку.
        int msgId = json["msg_id"].toInt();
        QMetaObject::invokeMethod(m_dbWorker, "updateMessageStatus", Qt::QueuedConnection, Q_ARG(int, msgId), Q_ARG(int, 1));
    }
}