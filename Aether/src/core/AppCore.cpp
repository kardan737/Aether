#include "AppCore.h"
#include <QDebug>
#include <QJsonObject>

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
    connect(this, &AppCore::sendJsonToNetwork, m_networkWorker, &NetworkWorker::sendJsonMessage);

    // Связи работы с контактами
    connect(this, &AppCore::requestLoadContacts, m_dbWorker, &DatabaseWorker::loadContacts);
    connect(this, &AppCore::requestAddContactToDb, m_dbWorker, &DatabaseWorker::addContact);

    connect(m_dbWorker, &DatabaseWorker::contactsLoaded, m_contactsModel, &ContactsModel::setContacts);
    connect(m_dbWorker, &DatabaseWorker::contactAdded, m_contactsModel, &ContactsModel::appendContact);

    // Связи работы с сообщениями
    connect(this, &AppCore::requestLoadMessagesFromDb, m_dbWorker, &DatabaseWorker::loadMessages);
    connect(this, &AppCore::requestAddMessageToDb, m_dbWorker, &DatabaseWorker::addMessage);
    connect(this, &AppCore::requestClearChatInDb, m_dbWorker, &DatabaseWorker::clearChat);

    connect(m_dbWorker, &DatabaseWorker::messagesLoaded, m_messagesModel, &MessagesModel::setMessages);
    connect(m_dbWorker, &DatabaseWorker::messageAdded, m_messagesModel, &MessagesModel::appendMessage);

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

void AppCore::requestAddContact(const QString& name) {
    emit requestAddContactToDb(name);
    
    // Пока в UI мы вводим IP-адрес вместо имени, пытаемся сразу открыть P2P-туннель
    QMetaObject::invokeMethod(m_networkWorker, "connectToPeer", Qt::QueuedConnection, 
                              Q_ARG(QString, name), Q_ARG(quint16, 7777));
}

void AppCore::requestLoadMessages(int contactId) {
    emit requestLoadMessagesFromDb(contactId);
}

void AppCore::requestSendMessage(int contactId, const QString& text) {
    emit requestAddMessageToDb(contactId, text, true, 0); // isMine=true, status=0 (ожидает)
    
    // Формируем полезную нагрузку (Payload)
    QJsonObject json;
    json["type"] = "message";
    json["text"] = text;
    
    // TODO: Нам нужно получить IP-адрес по contactId из DatabaseWorker для отправки,
    // либо отложить отправку до срабатывания таймера Store-and-Forward.
}

void AppCore::requestClearChat(int contactId) {
    emit requestClearChatInDb(contactId);
    m_messagesModel->clear(); // Очищаем и на фронтенде тоже
}

void AppCore::onNetworkMessageReceived(const QString& ip, const QJsonObject& json) {
    if (json.contains("type") && json["type"].toString() == "message") {
        QString text = json["text"].toString();
        qDebug() << "Aether P2P: Получено сообщение от" << ip << ":" << text;
        
        // TODO: Найти contactId по IP через DatabaseWorker и сохранить в БД
        // emit requestAddMessageToDb(contactId, text, false, 1);
    }
}