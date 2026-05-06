#include "DatabaseWorker.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QDateTime>
#include <QJsonObject>

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
        emit databaseInitialized(false, "Ошибка открытия БД: " + m_db.lastError().text());
        return;
    }

    createTables();
    emit databaseInitialized(true, "SQLite база данных успешно инициализирована.");

    // Запускаем таймер Store-and-Forward в потоке БД (каждые 5 секунд)
    if (!m_snfTimer) {
        m_snfTimer = new QTimer(this);
        connect(m_snfTimer, &QTimer::timeout, this, &DatabaseWorker::processStoreAndForward);
        m_snfTimer->start(5000);
    }
}

void DatabaseWorker::loadContacts() {
    QList<ContactData> contacts;
    QSqlQuery query(m_db);
    query.exec("SELECT id, name, last_seen FROM contacts");
    while (query.next()) {
        ContactData c;
        c.id = query.value(0).toInt();
        c.name = query.value(1).toString();
        c.isOnline = false; // В будущем будет зависеть от P2P сети
        c.decayLevel = 0.0; // В будущем: расчет старения на базе last_seen
        contacts.append(c);
    }
    emit contactsLoaded(contacts);
}

void DatabaseWorker::addContact(const QString& name, const QString& ip) {
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
        c.decayLevel = 0.0;
        emit contactAdded(c);
    } else {
        qWarning() << "Ошибка добавления контакта в БД:" << query.lastError().text();
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
        emit messageAdded(m);
        
        // Если это наше сообщение, достаем IP контакта и пробрасываем в сеть
        if (isMine) {
            QSqlQuery ipQuery(m_db);
            ipQuery.prepare("SELECT ip_address FROM contacts WHERE id = :cid");
            ipQuery.bindValue(":cid", contactId);
            if (ipQuery.exec() && ipQuery.next()) {
                QString ip = ipQuery.value(0).toString();
                QJsonObject json;
                json["type"] = "message";
                json["text"] = text;
                emit requestNetworkSend(m.id, ip, json);
            }
        }
    } else {
        qWarning() << "Ошибка добавления сообщения:" << query.lastError().text();
    }
}

void DatabaseWorker::processIncomingNetworkMessage(const QString& ip, const QString& text) {
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM contacts WHERE ip_address = :ip LIMIT 1");
    q.bindValue(":ip", ip);
    int contactId = -1;
    
    if (q.exec() && q.next()) {
        contactId = q.value(0).toInt();
    } else {
        // Неизвестный IP? Автоматически создаем контакт!
        q.prepare("INSERT INTO contacts (name, ip_address, last_seen) VALUES (:name, :ip, :ts)");
        q.bindValue(":name", ip); // В качестве имени ставим сам IP
        q.bindValue(":ip", ip);
        q.bindValue(":ts", QDateTime::currentSecsSinceEpoch());
        if (q.exec()) {
            contactId = q.lastInsertId().toInt();
            emit contactAdded(ContactData{contactId, ip, true, 0.0});
        }
    }
    
    // Сохраняем сообщение как входящее (isMine=false, status=1)
    addMessage(contactId, text, false, 1);
}

void DatabaseWorker::clearChat(int contactId) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM messages WHERE contact_id = :cid");
    query.bindValue(":cid", contactId);
    query.exec();
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

void DatabaseWorker::updateMessageStatus(int messageId, int status) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE messages SET status = :status WHERE id = :id");
    query.bindValue(":status", status);
    query.bindValue(":id", messageId);
    if (query.exec()) {
        emit messageStatusUpdated(messageId, status);
    }
}

void DatabaseWorker::processStoreAndForward() {
    QSqlQuery query(m_db);
    // Ищем зависшие сообщения, которые мы отправили, но статус всё еще 0
    query.exec("SELECT m.id, m.text, c.ip_address FROM messages m JOIN contacts c ON m.contact_id = c.id WHERE m.status = 0 AND m.is_mine = 1");
    while (query.next()) {
        QJsonObject json; json["type"] = "message"; json["text"] = query.value(1).toString();
        emit requestNetworkSend(query.value(0).toInt(), query.value(2).toString(), json);
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

    // Таблица сообщений (с поддержкой Store-and-Forward через status)
    query.exec("CREATE TABLE IF NOT EXISTS messages ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT, "
               "contact_id INTEGER, text TEXT, is_mine INTEGER, status INTEGER, timestamp INTEGER, "
               "FOREIGN KEY(contact_id) REFERENCES contacts(id))");
}