#include "DatabaseWorker.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QStandardPaths>
#include <QDir>
#include <QDebug>
#include <QDateTime>

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

void DatabaseWorker::addContact(const QString& name) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO contacts (name, last_seen) VALUES (:name, :last_seen)");
    query.bindValue(":name", name);
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
    } else {
        qWarning() << "Ошибка добавления сообщения:" << query.lastError().text();
    }
}

void DatabaseWorker::clearChat(int contactId) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM messages WHERE contact_id = :cid");
    query.bindValue(":cid", contactId);
    query.exec();
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