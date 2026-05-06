#include "NetworkWorker.h"
#include <QDebug>
#include <QHostAddress>
#include <QJsonDocument>
#include <QDataStream>

NetworkWorker::NetworkWorker(QObject *parent) : QObject(parent), m_server(new QTcpServer(this)) {
    connect(m_server, &QTcpServer::newConnection, this, &NetworkWorker::onNewConnection);
}

NetworkWorker::~NetworkWorker() {
    m_server->close();
    for (auto socket : m_clients.values()) {
        socket->disconnectFromHost();
        // deleteLater() не нужен, если parent - this, но явно почистить можно
    }
}

void NetworkWorker::startServer(quint16 port) {
    if (m_server->listen(QHostAddress::Any, port)) {
        emit serverStarted(true, QString("P2P Сервер запущен на порту %1").arg(port));
    } else {
        emit serverStarted(false, "Ошибка запуска сервера: " + m_server->errorString());
    }
}

void NetworkWorker::connectToPeer(const QString& ip, quint16 port) {
    if (m_clients.contains(ip)) {
        qDebug() << "Aether Network: Соединение с" << ip << "уже существует.";
        return;
    }

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, &NetworkWorker::onSocketConnected);
    connect(socket, &QTcpSocket::disconnected, this, &NetworkWorker::onSocketDisconnected);
    connect(socket, &QTcpSocket::readyRead, this, &NetworkWorker::onReadyRead);
    connect(socket, &QTcpSocket::errorOccurred, this, &NetworkWorker::onSocketError);

    // Записываем заранее, IP можно будет уточнить после подключения
    m_clients.insert(ip, socket); 
    socket->connectToHost(ip, port);
}

void NetworkWorker::sendJsonMessage(int messageId, const QString& ip, const QJsonObject& json) {
    if (!m_clients.contains(ip)) {
        // Если оффлайн - просто прерываем отправку без спама в консоль. 
        // Таймер Store-and-Forward позже заберет его из БД и попытается снова.
        return;
    }

    QTcpSocket* socket = m_clients.value(ip);
    if (socket->state() == QAbstractSocket::ConnectedState) {
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        
        QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
        out << (quint32)payload.size(); // Сначала записываем размер пакета (4 байта)
        block.append(payload);          // Затем сам JSON Payload
        
        socket->write(block);
        emit messageSent(messageId); // Уведомляем систему, что пакет ушел в сеть
    }
}

void NetworkWorker::onNewConnection() {
    while (m_server->hasPendingConnections()) {
        QTcpSocket* socket = m_server->nextPendingConnection();
        QString ip = socket->peerAddress().toString();
        
        // Убираем префикс IPv6-mapped IPv4, если он есть (например, ::ffff:192.168.1.5)
        if (ip.startsWith("::ffff:")) {
            ip = ip.mid(7);
        }

        connect(socket, &QTcpSocket::disconnected, this, &NetworkWorker::onSocketDisconnected);
        connect(socket, &QTcpSocket::readyRead, this, &NetworkWorker::onReadyRead);
        
        m_clients.insert(ip, socket);
        emit peerConnected(ip);
    }
}

void NetworkWorker::onReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    QString ip = socket->peerAddress().toString();
    
    // 1. Сливаем все новые байты в буфер конкретного клиента
    m_buffers[socket].append(socket->readAll());
    
    // 2. Пытаемся "вытащить" из буфера целые пакеты
    while (true) {
        if (m_buffers[socket].size() < sizeof(quint32)) {
            break; // Ждем, пока придет хотя бы размер пакета (первые 4 байта)
        }
        
        QDataStream in(&m_buffers[socket], QIODevice::ReadOnly);
        in.setVersion(QDataStream::Qt_6_0);
        
        quint32 packetSize = 0;
        in >> packetSize; // Читаем ожидаемый размер Payload'а
        
        // АРХИТЕКТУРНОЕ ТРЕБОВАНИЕ: Ограничение бинарников (100 МБ = 104857600 байт)
        if (packetSize > 104857600) {
            qWarning() << "Aether: Пакет превышает 100 МБ! Разрываем соединение в целях безопасности.";
            socket->disconnectFromHost();
            return;
        }
        
        if (m_buffers[socket].size() < sizeof(quint32) + packetSize) {
            break; // Пакет пришел не полностью (TCP фрагментация). Ждем дальше.
        }
        
        // Пакет полностью в памяти! Извлекаем его.
        QByteArray payload = m_buffers[socket].mid(sizeof(quint32), packetSize);
        m_buffers[socket].remove(0, sizeof(quint32) + packetSize);
        
        // Парсим JSON
        QJsonParseError parseError;
        QJsonDocument jsonDoc = QJsonDocument::fromJson(payload, &parseError);
        if (parseError.error == QJsonParseError::NoError && jsonDoc.isObject()) {
            emit messageReceived(ip, jsonDoc.object());
        } else {
            qWarning() << "Aether: Ошибка парсинга JSON от" << ip;
        }
    }
}

void NetworkWorker::onSocketConnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    QString ip = socket->peerAddress().toString();
    emit peerConnected(ip);
}

void NetworkWorker::onSocketDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    QString ip = socket->peerAddress().toString();
    
    m_clients.remove(ip);
    m_buffers.remove(socket); // Обязательно очищаем буфер при отключении
    socket->deleteLater(); // Обязательно освобождаем память асинхронно
    emit peerDisconnected(ip);
}

void NetworkWorker::onSocketError(QAbstractSocket::SocketError socketError) {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        qWarning() << "Aether Network Error:" << socket->errorString();
        // Ошибки подключения (например, собеседник оффлайн) будут обрабатываться тут
    }
}