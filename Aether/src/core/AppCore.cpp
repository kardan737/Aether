#include "AppCore.h"
#include <QDebug>

AppCore::AppCore(QObject *parent) : QObject(parent) {
    // Обязательная регистрация типов для работы через Qt::QueuedConnection между потоками
    qRegisterMetaType<ContactData>("ContactData");
    qRegisterMetaType<QList<ContactData>>("QList<ContactData>");
    qRegisterMetaType<MessageData>("MessageData");
    qRegisterMetaType<QList<MessageData>>("QList<MessageData>");

    m_contactsModel = new ContactsModel(this);
    m_messagesModel = new MessagesModel(this);
    m_dbWorker = new DatabaseWorker();
    
    m_dbWorker->moveToThread(&m_dbThread);

    connect(&m_dbThread, &QThread::finished, m_dbWorker, &QObject::deleteLater);

    // Связи инициализации
    connect(this, &AppCore::startDbInit, m_dbWorker, &DatabaseWorker::initDatabase);
    connect(m_dbWorker, &DatabaseWorker::databaseInitialized, this, &AppCore::onDbInitialized);
    
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
}

AppCore::~AppCore() {
    m_dbThread.quit();
    m_dbThread.wait();
}

void AppCore::initialize() {
    emit startDbInit();
}

void AppCore::onDbInitialized(bool success, const QString& message) {
    qDebug() << "Aether Backend:" << message;
    if (success) {
        emit requestLoadContacts(); // После запуска БД сразу просим загрузить контакты
    }
}

void AppCore::requestAddContact(const QString& name) {
    emit requestAddContactToDb(name);
}

void AppCore::requestLoadMessages(int contactId) {
    emit requestLoadMessagesFromDb(contactId);
}

void AppCore::requestSendMessage(int contactId, const QString& text) {
    emit requestAddMessageToDb(contactId, text, true, 0); // isMine=true, status=0 (ожидает)
}

void AppCore::requestClearChat(int contactId) {
    emit requestClearChatInDb(contactId);
    m_messagesModel->clear(); // Очищаем и на фронтенде тоже
}