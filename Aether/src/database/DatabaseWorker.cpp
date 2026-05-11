#include "DatabaseWorker.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QJsonObject>
#include <QSettings>
#include <QFile>
#include <QFileInfo>

DatabaseWorker::DatabaseWorker(QObject *parent) : QObject(parent) {}

DatabaseWorker::~DatabaseWorker() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

void DatabaseWorker::initDatabase() {
    // Важно: Соединение создается в том потоке, в котором находится объект
    m_db = QSqlDatabase::addDatabase("QSQLITE", "aether_db_connection");
    
    // Сохраняем базу в системную папку AppData
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/Aether";
    QDir dir;
    if (!dir.exists(dataPath)) {
        dir.mkpath(dataPath);
    }
    
    m_db.setDatabaseName(dataPath + "/aether.db");

    if (!m_db.open()) {
        emit databaseInitialized(false, "Database open error: " + m_db.lastError().text());
        return;
    }

    createTables();
    emit databaseInitialized(true, "SQLite database successfully initialized.");

    // Запускаем таймер Store-and-Forward в потоке БД (каждые 5 секунд)
    if (!m_snfTimer) {
        m_snfTimer = new QTimer(this);
        connect(m_snfTimer, &QTimer::timeout, this, &DatabaseWorker::processStoreAndForward);
        m_snfTimer->start(5000);
    }

    // Запускаем таймер Пинга (каждые 10 секунд проверяем, кто онлайн)
    if (!m_pingTimer) {
        m_pingTimer = new QTimer(this);
        connect(m_pingTimer, &QTimer::timeout, this, &DatabaseWorker::processPing);
        m_pingTimer->start(10000); // 10000 мс = 10 секунд
    }
}

void DatabaseWorker::loadContacts() {
    QList<ContactData> contacts;
    QSqlQuery query(m_db);
    // Умный запрос: вытягиваем контакты и заодно текст их последнего сообщения
    query.exec("SELECT c.id, c.name, c.last_seen, c.unread_count, "
               "(SELECT text FROM messages m WHERE m.contact_id = c.id ORDER BY m.timestamp DESC LIMIT 1), "
               "c.original_name "
               "FROM contacts c ORDER BY c.last_seen DESC");
    while (query.next()) {
        ContactData c;
        c.id = query.value(0).toInt();
        c.name = query.value(1).toString();
        c.isOnline = false; // В будущем будет зависеть от P2P сети
        c.unreadCount = query.value(3).toInt();
        c.lastMessage = query.value(4).toString();
        c.originalName = query.value(5).toString();
        contacts.append(c);
    }
    emit contactsLoaded(contacts);
    
    // Вызываем пинг сразу после загрузки контактов, чтобы моментально обновить статусы при старте
    processPing();
}

void DatabaseWorker::addContact(const QString& name, const QString& ip) {
    QSqlQuery check(m_db);
    check.prepare("SELECT id FROM contacts WHERE ip_address = :ip");
    check.bindValue(":ip", ip);
    // Защита от дубликатов: если IP уже есть, игнорируем добавление
    if (check.exec() && check.next()) {
        qWarning() << "Aether: Contact with this IP already exists!";
        return; 
    }

    QSqlQuery query(m_db);
    query.prepare("INSERT INTO contacts (name, ip_address, last_seen) VALUES (:name, :ip, :last_seen)");
    query.bindValue(":name", name);
    query.bindValue(":ip", ip);
    query.bindValue(":last_seen", QDateTime::currentSecsSinceEpoch());
    
    if (query.exec()) {
        ContactData c;
        c.id = query.lastInsertId().toInt();
        c.name = name;
        c.isOnline = false;
        c.unreadCount = 0;
        c.lastMessage = "";
        c.originalName = "";
        emit contactAdded(c);
    } else {
        qWarning() << "Error adding contact to DB:" << query.lastError().text();
    }
}

void DatabaseWorker::loadMessages(int contactId) {
    QList<MessageData> messages;
    QSqlQuery query(m_db);
    query.prepare("SELECT id, text, is_mine, status, timestamp FROM messages WHERE contact_id = :cid ORDER BY timestamp ASC");
    query.bindValue(":cid", contactId);
    query.exec();
    
    while (query.next()) {
        MessageData m;
        m.id = query.value(0).toInt();
        m.text = query.value(1).toString();
        m.isMine = query.value(2).toInt() != 0;
        m.status = query.value(3).toInt();
        qint64 ts = query.value(4).toLongLong();
        m.time = QDateTime::fromSecsSinceEpoch(ts).toString("HH:mm");
        messages.append(m);
    }
    emit messagesLoaded(messages);
}

void DatabaseWorker::addFileMessage(int contactId, const QString& localPath) {
    QString text = "FILE:" + localPath;
    addMessage(contactId, text, true, 0); // Используем ту же логику сохранения!
}

void DatabaseWorker::addMessage(int contactId, const QString& text, bool isMine, int status) {
    QSqlQuery query(m_db);
    qint64 ts = QDateTime::currentSecsSinceEpoch();
    query.prepare("INSERT INTO messages (contact_id, text, is_mine, status, timestamp) VALUES (:cid, :txt, :ism, :st, :ts)");
    query.bindValue(":cid", contactId);
    query.bindValue(":txt", text);
    query.bindValue(":ism", isMine ? 1 : 0);
    query.bindValue(":st", status);
    query.bindValue(":ts", ts);
    
    if (query.exec()) {
        MessageData m{query.lastInsertId().toInt(), text, isMine, status, QDateTime::fromSecsSinceEpoch(ts).toString("HH:mm")};
        emit messageAdded(contactId, m);
        
        // Обновляем время последней активности контакта и поднимаем его наверх
        QSqlQuery updateContact(m_db);
        updateContact.prepare("UPDATE contacts SET last_seen = :ts WHERE id = :cid");
        updateContact.bindValue(":ts", ts);
        updateContact.bindValue(":cid", contactId);
        updateContact.exec();
        emit contactMovedToTop(contactId);
        emit contactLastMessageChanged(contactId, text); // Обновляем предпросмотр сообщения
        
        // Если это наше сообщение, достаем IP контакта и пробрасываем в сеть
        if (isMine) {
            QSqlQuery ipQuery(m_db);
            ipQuery.prepare("SELECT ip_address FROM contacts WHERE id = :cid");
            ipQuery.bindValue(":cid", contactId);
            if (ipQuery.exec() && ipQuery.next()) {
                QString ip = ipQuery.value(0).toString();
                QJsonObject json;
                json["type"] = "message";
                json["msg_id"] = m.id; // Передаем ID для получения истинной галочки
                json["text"] = text;
                
                // Подцепляем наше имя из настроек для отправки другу
                QSettings settings;
                json["sender_name"] = settings.value("myName", "Аноним").toString();
                
                if (text.startsWith("FILE:")) {
                    QString path = text.mid(5);
                    QFile file(path);
                    if (file.open(QIODevice::ReadOnly)) {
                        json["type"] = "file";
                        json["filename"] = QFileInfo(path).fileName();
                        json["data"] = QString(file.readAll().toBase64());
                    } else {
                        return; // Не смогли прочитать файл
                    }
                } else {
                    json["type"] = "message";
                    json["text"] = text;
                }
                
                emit requestNetworkSend(m.id, ip, json);
                
                m_inFlightMessages.insert(m.id); // Помечаем, что процесс отправки запущен
            }
        }
    } else {
        qWarning() << "Error adding message:" << query.lastError().text();
    }
}

void DatabaseWorker::processIncomingNetworkMessage(const QString& ip, const QString& text, const QString& senderName) {
    QSqlQuery q(m_db);
    q.prepare("SELECT id, name FROM contacts WHERE ip_address = :ip LIMIT 1");
    q.bindValue(":ip", ip);
    int contactId = -1;
    
    QString finalName = senderName.isEmpty() ? ip : senderName;

    if (q.exec() && q.next()) {
        contactId = q.value(0).toInt();
        QString currentName = q.value(1).toString();
        
        // Если чат назывался IP-адресом, а теперь мы узнали настоящее имя друга — переименовываем!
        if (currentName == ip && !senderName.isEmpty() && senderName != ip) {
            QSqlQuery updateQ(m_db);
            updateQ.prepare("UPDATE contacts SET name = :name WHERE id = :id");
            updateQ.bindValue(":name", finalName);
            updateQ.bindValue(":id", contactId);
            updateQ.exec();
            loadContacts(); // Обновляем список слева
        }
    } else {
        // Неизвестный IP? Автоматически создаем контакт!
        q.prepare("INSERT INTO contacts (name, ip_address, last_seen) VALUES (:name, :ip, :ts)");
        q.bindValue(":name", finalName); // Ставим имя из пакета
        q.bindValue(":ip", ip);
        q.bindValue(":ts", QDateTime::currentSecsSinceEpoch());
        if (q.exec()) {
            contactId = q.lastInsertId().toInt();
            emit contactAdded(ContactData{contactId, finalName, true, 0, "", senderName});
        }
    }
    
    // Обновляем "настоящее имя" контакта, если друг прислал свой никнейм
    if (!senderName.isEmpty() && senderName != ip) {
        QSqlQuery origQ(m_db);
        origQ.prepare("UPDATE contacts SET original_name = :orig WHERE id = :id");
        origQ.bindValue(":orig", senderName);
        origQ.bindValue(":id", contactId);
        if (origQ.exec()) {
            emit contactOriginalNameChanged(contactId, senderName);
        }
    }
    
    // Сохраняем сообщение как входящее (isMine=false, status=1)
    addMessage(contactId, text, false, 1);
    
    // Увеличиваем счетчик непрочитанных сообщений для этого контакта
    QSqlQuery updateUnread(m_db);
    updateUnread.prepare("UPDATE contacts SET unread_count = unread_count + 1 WHERE id = :id");
    updateUnread.bindValue(":id", contactId);
    if (updateUnread.exec()) {
        QSqlQuery fetchUnread(m_db);
        fetchUnread.prepare("SELECT unread_count FROM contacts WHERE id = :id");
        fetchUnread.bindValue(":id", contactId);
        if (fetchUnread.exec() && fetchUnread.next()) {
            emit contactUnreadCountChanged(contactId, fetchUnread.value(0).toInt());
        }
    }
}

void DatabaseWorker::processIncomingFileMessage(const QString& ip, const QString& filename, const QByteArray& data, const QString& senderName) {
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM contacts WHERE ip_address = :ip LIMIT 1");
    q.bindValue(":ip", ip);
    int contactId = -1;
    
    QString finalName = senderName.isEmpty() ? ip : senderName;
    if (q.exec() && q.next()) {
        contactId = q.value(0).toInt();
    } else {
        q.prepare("INSERT INTO contacts (name, ip_address, last_seen) VALUES (:name, :ip, :ts)");
        q.bindValue(":name", finalName); q.bindValue(":ip", ip); q.bindValue(":ts", QDateTime::currentSecsSinceEpoch());
        if (q.exec()) {
            contactId = q.lastInsertId().toInt();
            emit contactAdded(ContactData{contactId, finalName, true, 0, "", senderName});
        }
    }
    
    // Обновляем "настоящее имя" для файлов тоже
    if (!senderName.isEmpty() && senderName != ip) {
        QSqlQuery origQ(m_db);
        origQ.prepare("UPDATE contacts SET original_name = :orig WHERE id = :id");
        origQ.bindValue(":orig", senderName);
        origQ.bindValue(":id", contactId);
        if (origQ.exec()) {
            emit contactOriginalNameChanged(contactId, senderName);
        }
    }
    
    // Сохраняем файл физически на диск!
    QString downloadsPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/AetherDownloads";
    QDir().mkpath(downloadsPath);
    
    QString filePath = downloadsPath + "/" + filename;
    int counter = 1;
    while (QFile::exists(filePath)) { // Защита от одинаковых имен
        filePath = downloadsPath + "/" + QString::number(counter++) + "_" + filename;
    }
    
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.close();
    }
    
    QString text = "FILE:" + filePath;
    addMessage(contactId, text, false, 1); // Сохраняем в БД как файл
    
    // Обновляем счетчик
    QSqlQuery updateUnread(m_db);
    updateUnread.prepare("UPDATE contacts SET unread_count = unread_count + 1 WHERE id = :id");
    updateUnread.bindValue(":id", contactId);
    if (updateUnread.exec()) {
        QSqlQuery fetchUnread(m_db);
        fetchUnread.prepare("SELECT unread_count FROM contacts WHERE id = :id");
        fetchUnread.bindValue(":id", contactId);
        if (fetchUnread.exec() && fetchUnread.next()) {
            emit contactUnreadCountChanged(contactId, fetchUnread.value(0).toInt());
        }
    }
}

void DatabaseWorker::clearChat(int contactId) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM messages WHERE contact_id = :cid");
    query.bindValue(":cid", contactId);
    if (query.exec()) {
        emit contactLastMessageChanged(contactId, "");
    }
}

void DatabaseWorker::deleteContact(int contactId) {
    QSqlQuery query(m_db);
    // Сначала удаляем все сообщения чата
    query.prepare("DELETE FROM messages WHERE contact_id = :cid");
    query.bindValue(":cid", contactId);
    query.exec();
    
    // Затем удаляем сам контакт
    query.prepare("DELETE FROM contacts WHERE id = :cid");
    query.bindValue(":cid", contactId);
    if (query.exec()) {
        emit contactDeleted(contactId);
    }
}

void DatabaseWorker::renameContact(int contactId, const QString& newName) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE contacts SET name = :name WHERE id = :id");
    query.bindValue(":name", newName);
    query.bindValue(":id", contactId);
    if (query.exec()) {
        emit contactRenamed(contactId, newName);
    } else {
        qWarning() << "Error renaming contact in DB:" << query.lastError().text();
    }
}

void DatabaseWorker::updateMessageStatus(int messageId, int status) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE messages SET status = :status WHERE id = :id");
    query.bindValue(":status", status);
    query.bindValue(":id", messageId);
    if (query.exec()) {
        emit messageStatusUpdated(messageId, status);
    }
    
    if (status == 1) { // Если доставлено - убираем из списка In-Flight
        m_inFlightMessages.remove(messageId);
    }
}

void DatabaseWorker::clearCache() {
    QString downloadsPath = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + "/AetherDownloads";
    QDir dir(downloadsPath);
    if (dir.exists()) {
        QStringList files = dir.entryList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QString& file : files) {
            dir.remove(file); // Физически удаляем файл с жесткого диска
        }
        qDebug() << "Aether: Cache cleared, files deleted:" << files.size();
    }
}

void DatabaseWorker::markChatAsRead(int contactId) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE contacts SET unread_count = 0 WHERE id = :id");
    query.bindValue(":id", contactId);
    if (query.exec()) {
        emit contactUnreadCountChanged(contactId, 0);
    }
}

void DatabaseWorker::handlePeerConnected(const QString& ip) {
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM contacts WHERE ip_address = :ip");
    q.bindValue(":ip", ip);
    if (q.exec()) {
        while (q.next()) {
            emit contactStatusChanged(q.value(0).toInt(), true);
        }
    }
    
    // Мгновенно пытаемся отправить зависшие сообщения, как только узел появился в сети!
    processStoreAndForward();
}

void DatabaseWorker::handlePeerDisconnected(const QString& ip) {
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM contacts WHERE ip_address = :ip");
    q.bindValue(":ip", ip);
    if (q.exec()) {
        while (q.next()) {
            emit contactStatusChanged(q.value(0).toInt(), false);
            
            // Друг отключился. Очищаем список In-Flight для его сообщений, 
            // чтобы таймер снова подхватил их, когда друг вернется в сеть.
            int cid = q.value(0).toInt();
            QSqlQuery msgQ(m_db);
            msgQ.prepare("SELECT id FROM messages WHERE contact_id = :cid AND status = 0");
            msgQ.bindValue(":cid", cid);
            if (msgQ.exec()) {
                while (msgQ.next()) {
                    m_inFlightMessages.remove(msgQ.value(0).toInt());
                }
            }
        }
    }
}

void DatabaseWorker::handleMessageSendFailed(int messageId) {
    m_inFlightMessages.remove(messageId);
}

void DatabaseWorker::processStoreAndForward() {
    QSqlQuery query(m_db);
    // Ищем зависшие сообщения, которые мы отправили, но статус всё еще 0
    query.exec("SELECT m.id, m.text, c.ip_address FROM messages m JOIN contacts c ON m.contact_id = c.id WHERE m.status = 0 AND m.is_mine = 1");
    
    QSettings settings;
    QString myName = settings.value("myName", "Аноним").toString();
    
    while (query.next()) {
        int msgId = query.value(0).toInt();
        if (m_inFlightMessages.contains(msgId)) continue; // Файл УЖЕ отправляется, пропускаем!

        QJsonObject json; json["type"] = "message"; json["msg_id"] = query.value(0).toInt(); json["text"] = query.value(1).toString(); json["sender_name"] = myName;
        
        QString text = query.value(1).toString();
        if (text.startsWith("FILE:")) {
            QString path = text.mid(5);
            QFile file(path);
            if (file.open(QIODevice::ReadOnly)) {
                json["type"] = "file";
                json["filename"] = QFileInfo(path).fileName();
                json["data"] = QString(file.readAll().toBase64());
            } else { continue; }
        } else {
            json["type"] = "message";
            json["text"] = text;
        }
        
        emit requestNetworkSend(msgId, query.value(2).toString(), json);
        m_inFlightMessages.insert(msgId);
    }
}

void DatabaseWorker::processPing() {
    QSqlQuery query(m_db);
    query.exec("SELECT ip_address FROM contacts");
    while (query.next()) {
        emit requestNetworkConnect(query.value(0).toString(), 7777);
    }
}

void DatabaseWorker::createTables() {
    QSqlQuery query(m_db);
    
    // Таблица контактов (с поддержкой Data Decay через last_seen)
    query.exec("CREATE TABLE IF NOT EXISTS contacts ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
               "name TEXT UNIQUE NOT NULL, "
               "ip_address TEXT, "
               "last_seen INTEGER)");
               
    // Мягкое добавление колонки (для старых баз данных, сработает только 1 раз)
    query.exec("ALTER TABLE contacts ADD COLUMN unread_count INTEGER DEFAULT 0");
    query.exec("ALTER TABLE contacts ADD COLUMN original_name TEXT DEFAULT ''");

    // Таблица сообщений (с поддержкой Store-and-Forward через status)
    query.exec("CREATE TABLE IF NOT EXISTS messages ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
               "contact_id INTEGER, text TEXT, is_mine INTEGER, status INTEGER, timestamp INTEGER, "
               "FOREIGN KEY(contact_id) REFERENCES contacts(id))");
}