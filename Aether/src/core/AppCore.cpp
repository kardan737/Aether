#include "AppCore.h"
#include <QDebug>
#include <QJsonObject>
#include <QHostAddress>
#include <QNetworkInterface>
#include <QFileInfo>
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QImage>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QIcon>
#include <QPixmap>

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
    
    // Создаем системный трей для уведомлений
    if (QSystemTrayIcon::isSystemTrayAvailable()) {
        m_trayIcon = new QSystemTrayIcon(QIcon(":/qt/qml/Aether/icons/logo.png"), this);
        m_trayIcon->show();
    } else {
        qWarning() << "Aether: System tray is not available on this system.";
    }

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
    connect(m_networkWorker, &NetworkWorker::messageSendFailed, m_dbWorker, &DatabaseWorker::handleMessageSendFailed);
    connect(m_networkWorker, &NetworkWorker::messageUploadProgress, this, &AppCore::onMessageUploadProgress);

    connect(this, &AppCore::sendJsonToNetwork, m_networkWorker, &NetworkWorker::sendJsonMessage);

    // Прямая связь: БД напрямую просит Сеть отправить пакет (работает через QueuedConnection)
    connect(m_dbWorker, &DatabaseWorker::requestNetworkSend, m_networkWorker, &NetworkWorker::sendJsonMessage);
    connect(m_dbWorker, &DatabaseWorker::requestNetworkConnect, m_networkWorker, &NetworkWorker::connectToPeer);
    connect(this, &AppCore::requestProcessIncomingNetworkMessage, m_dbWorker, &DatabaseWorker::processIncomingNetworkMessage);
    connect(this, &AppCore::requestProcessIncomingFileMessage, m_dbWorker, &DatabaseWorker::processIncomingFileMessage);

    // Связи работы с контактами
    connect(this, &AppCore::requestLoadContacts, m_dbWorker, &DatabaseWorker::loadContacts);
    connect(this, &AppCore::requestAddContactToDb, m_dbWorker, &DatabaseWorker::addContact);

    connect(m_dbWorker, &DatabaseWorker::contactsLoaded, m_contactsModel, &ContactsModel::setContacts);
    connect(m_dbWorker, &DatabaseWorker::contactAdded, m_contactsModel, &ContactsModel::appendContact);

    // Связи работы с сообщениями
    connect(this, &AppCore::requestLoadMessagesFromDb, m_dbWorker, &DatabaseWorker::loadMessages);
    connect(this, &AppCore::requestAddMessageToDb, m_dbWorker, &DatabaseWorker::addMessage);
    connect(this, &AppCore::requestAddFileMessageToDb, m_dbWorker, &DatabaseWorker::addFileMessage);
    connect(this, &AppCore::requestClearChatInDb, m_dbWorker, &DatabaseWorker::clearChat);
    connect(this, &AppCore::requestDeleteContactInDb, m_dbWorker, &DatabaseWorker::deleteContact);
    connect(this, &AppCore::requestDeleteMessageInDb, m_dbWorker, &DatabaseWorker::deleteMessage);
    connect(this, &AppCore::requestRenameContactInDb, m_dbWorker, &DatabaseWorker::renameContact);
    connect(this, &AppCore::requestMarkChatAsReadInDb, m_dbWorker, &DatabaseWorker::markChatAsRead);
    connect(this, &AppCore::requestClearCacheInDb, m_dbWorker, &DatabaseWorker::clearCache);

    connect(m_dbWorker, &DatabaseWorker::messagesLoaded, m_messagesModel, &MessagesModel::setMessages);
    connect(m_dbWorker, &DatabaseWorker::messageAdded, this, &AppCore::onMessageAdded);
    connect(m_dbWorker, &DatabaseWorker::contactDeleted, m_contactsModel, &ContactsModel::removeContact);
    connect(m_dbWorker, &DatabaseWorker::messageDeleted, m_messagesModel, &MessagesModel::removeMessage);
    connect(m_dbWorker, &DatabaseWorker::contactRenamed, m_contactsModel, &ContactsModel::updateContactName);
    connect(m_dbWorker, &DatabaseWorker::contactMovedToTop, m_contactsModel, &ContactsModel::moveContactToTop);
    connect(m_dbWorker, &DatabaseWorker::contactUnreadCountChanged, m_contactsModel, &ContactsModel::updateContactUnreadCount);
    connect(m_dbWorker, &DatabaseWorker::contactLastMessageChanged, m_contactsModel, &ContactsModel::updateContactLastMessage);
    connect(m_dbWorker, &DatabaseWorker::contactOriginalNameChanged, m_contactsModel, &ContactsModel::updateContactOriginalName);
    connect(m_dbWorker, &DatabaseWorker::contactOriginalNameChanged, this, &AppCore::originalNameUpdated);

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
    m_currentContactId = contactId;
    emit requestLoadMessagesFromDb(contactId);
}

void AppCore::requestSendMessage(int contactId, const QString& text, const QString& replyText) {
    emit requestAddMessageToDb(contactId, text, true, 0, replyText);
    // Отправка в сеть теперь автоматически произойдет внутри DatabaseWorker::addMessage
}

void AppCore::requestSendFile(int contactId, const QUrl& fileUrl, const QString& replyText) {
    QString localPath = fileUrl.toLocalFile();
    QFileInfo fi(localPath);
    // Жесткое ограничение 100 МБ для локального файла
    if (fi.size() > 100 * 1024 * 1024) {
        qWarning() << "Aether: File is larger than 100 MB!";
        return;
    }
    emit requestAddFileMessageToDb(contactId, localPath, replyText);
}

bool AppCore::requestPasteFromClipboard(int contactId, const QString& replyText) {
    if (contactId == -1) return false;
    
    const QClipboard *clipboard = QGuiApplication::clipboard();
    const QMimeData *mimeData = clipboard->mimeData();
    if (!mimeData) return false;

    // 1. Проверяем, есть ли файлы (скопированные из проводника)
    if (mimeData->hasUrls() && !mimeData->urls().isEmpty()) {
        bool handled = false;
        for (const QUrl &url : mimeData->urls()) {
            if (url.isLocalFile()) {
                requestSendFile(contactId, url, replyText);
                handled = true;
            }
        }
        if (handled) return true; // Прерываем стандартную вставку текста
    }
    
    // 2. Проверяем, есть ли картинка (скопированная из "Ножниц" или браузера)
    if (mimeData->hasImage()) {
        QImage image = qvariant_cast<QImage>(mimeData->imageData());
        if (!image.isNull()) {
            QString downloadsPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/AetherDownloads";
            QDir().mkpath(downloadsPath);
            
            // Сохраняем скриншот во временный файл и отправляем
            QString filePath = downloadsPath + "/clipboard_" + QString::number(QDateTime::currentMSecsSinceEpoch()) + ".png";
            if (image.save(filePath, "PNG")) {
                emit requestAddFileMessageToDb(contactId, filePath, replyText);
                return true; // Прерываем стандартную вставку
            }
        }
    }

    return false; // Это обычный текст, возвращаем false, чтобы QML вставил его в поле
}

void AppCore::copyToClipboard(const QString& text) {
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (clipboard) {
        clipboard->setText(text);
    }
}

void AppCore::requestClearChat(int contactId) {
    emit requestClearChatInDb(contactId);
    if (m_currentContactId == contactId) {
        m_messagesModel->clear(); // Очищаем на фронтенде, только если это текущий чат
    }
}

void AppCore::requestDeleteContact(int contactId) {
    emit requestDeleteContactInDb(contactId);
    if (m_currentContactId == contactId) {
        m_messagesModel->clear(); // Очищаем сообщения на экране
        m_currentContactId = -1;
    }
}

void AppCore::requestDeleteMessage(int messageId) {
    emit requestDeleteMessageInDb(messageId);
}

void AppCore::requestRenameContact(int contactId, const QString& newName) {
    emit requestRenameContactInDb(contactId, newName);
}

void AppCore::requestMarkChatAsRead(int contactId) {
    emit requestMarkChatAsReadInDb(contactId);
}

void AppCore::requestClearCache() {
    emit requestClearCacheInDb();
}

QString AppCore::getLocalIpAddress() {
    QStringList ipList;
    const QList<QHostAddress> addresses = QNetworkInterface::allAddresses();
    for (const QHostAddress &address : addresses) {
        // Берем только IPv4 и исключаем локальный 127.0.0.1
        if (address.protocol() == QAbstractSocket::IPv4Protocol && !address.isLoopback()) {
            ipList.append(address.toString());
        }
    }
    return ipList.isEmpty() ? "Неизвестно" : ipList.join("\n");
}

void AppCore::onNetworkMessageReceived(const QString& ip, const QJsonObject& json) {
    QString type = json["type"].toString();
    if (type == "message") {
        QString text = json["text"].toString();
        QString senderName = json.value("sender_name").toString(); // Достаем имя друга
        QString replyText = json.value("reply_text").toString();
        
        qDebug() << "Aether P2P: Received message from" << (senderName.isEmpty() ? ip : senderName) << ":" << text;
        
        // Просим базу данных разобраться, чей это IP, и сохранить текст
        emit requestProcessIncomingNetworkMessage(ip, text, senderName, replyText);
        
        // Отправляем "истинную галочку" (ACK) обратно собеседнику
        if (json.contains("msg_id")) {
            QJsonObject ack;
            ack["type"] = "ack";
            ack["msg_id"] = json["msg_id"].toInt();
            emit sendJsonToNetwork(0, ip, ack); // ID 0, так как само подтверждение не нужно отслеживать
        }
    } else if (type == "file") {
        QString filename = json["filename"].toString();
        QByteArray fileData = QByteArray::fromBase64(json["data"].toString().toUtf8());
        QString senderName = json.value("sender_name").toString();
        QString replyText = json.value("reply_text").toString();
        
        qDebug() << "Aether P2P: Received FILE from" << (senderName.isEmpty() ? ip : senderName) << ":" << filename;
        emit requestProcessIncomingFileMessage(ip, filename, fileData, senderName, replyText);
        
        if (json.contains("msg_id")) {
            QJsonObject ack; ack["type"] = "ack"; ack["msg_id"] = json["msg_id"].toInt();
            emit sendJsonToNetwork(0, ip, ack);
        }
    } else if (type == "ack") {
        // Собеседник подтвердил получение! Теперь ставим галочку.
        int msgId = json["msg_id"].toInt();
        QMetaObject::invokeMethod(m_dbWorker, "updateMessageStatus", Qt::QueuedConnection, Q_ARG(int, msgId), Q_ARG(int, 1));
    }
}

void AppCore::onMessageAdded(int contactId, const MessageData& message) {
    if (m_currentContactId == contactId) {
        m_messagesModel->appendMessage(message);
    }
    
    // Системное уведомление при получении нового сообщения (игнорируем свои собственные отправки)
    if (!message.isMine && m_trayIcon && QApplication::applicationState() != Qt::ApplicationActive) {
        QString contactName = m_contactsModel->getContactName(contactId);
        QString displayMsg = message.text;
        
        if (displayMsg.startsWith("FILE:")) {
            QString path = displayMsg.mid(5);
            displayMsg = "📎 Файл: " + QFileInfo(path).fileName();
        }
        
        QMetaObject::invokeMethod(m_trayIcon, [this, contactName, displayMsg]() {
            m_trayIcon->showMessage(contactName, displayMsg, QSystemTrayIcon::MessageIcon::Information, 3000);
        }, Qt::QueuedConnection);
    }
}

void AppCore::onMessageUploadProgress(int messageId, double progress) {
    m_messagesModel->updateMessageProgress(messageId, progress);
}