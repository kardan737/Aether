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
        emit serverStarted(true, QString("P2P Server started on port %1").arg(port));
    } else {
        emit serverStarted(false, "Server startup error: " + m_server->errorString());
    }
}

void NetworkWorker::connectToPeer(const QString& ip, quint16 port) {
    if (m_clients.contains(ip)) {
        if (m_clients.value(ip)->state() == QAbstractSocket::ConnectedState) return;
        // Если сокет сломан или завис, удаляем его и подключаемся заново
        QTcpSocket* oldSocket = m_clients.value(ip);
        m_clients.remove(ip);
        oldSocket->disconnectFromHost();
        oldSocket->deleteLater();
    }

    QTcpSocket* socket = new QTcpSocket(this);
    connect(socket, &QTcpSocket::connected, this, &NetworkWorker::onSocketConnected);
    connect(socket, &QTcpSocket::disconnected, this, &NetworkWorker::onSocketDisconnected);
    connect(socket, &QTcpSocket::readyRead, this, &NetworkWorker::onReadyRead);
    connect(socket, &QTcpSocket::bytesWritten, this, &NetworkWorker::onBytesWritten);
    connect(socket, &QTcpSocket::errorOccurred, this, &NetworkWorker::onSocketError);

    // Записываем заранее, IP можно будет уточнить после подключения
    m_clients.insert(ip, socket); 
    socket->connectToHost(ip, port);
}

void NetworkWorker::sendJsonMessage(int messageId, const QString& ip, const QJsonObject& json) {
    // 1. Для ФАЙЛОВ открываем выделенный параллельный канал передачи данных
    if (json.value("type").toString() == "file") {
        qDebug() << "Aether P2P: Opening dedicated data channel for FILE to" << ip;
        QTcpSocket* fileSocket = new QTcpSocket(this);
        
        connect(fileSocket, &QTcpSocket::connected, this, [this, fileSocket, json, messageId]() {
            QByteArray block;
            QDataStream out(&block, QIODevice::WriteOnly);
            out.setVersion(QDataStream::Qt_6_0);
            QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
            out << (quint32)payload.size();
            block.append(payload);
            
            qint64 chunkSize = 512 * 1024; // Кормим ОС кусочками по 512 КБ
            qint64 toPush = qMin(chunkSize, (qint64)block.size());
            
            m_chunkedWriters[fileSocket] = {block, toPush, 0, messageId};
            if (messageId > 0) emit messageUploadProgress(messageId, 0.0);
            
            fileSocket->write(block.constData(), toPush);
        });
        
        connect(fileSocket, &QTcpSocket::bytesWritten, this, &NetworkWorker::onBytesWritten);
        
        connect(fileSocket, &QTcpSocket::disconnected, this, [this, fileSocket]() {
            m_chunkedWriters.remove(fileSocket);
            fileSocket->deleteLater();
        });
        connect(fileSocket, &QTcpSocket::errorOccurred, this, [this, fileSocket, messageId](QAbstractSocket::SocketError err) {
            bool wasComplete = false;
            if (m_chunkedWriters.contains(fileSocket)) {
                wasComplete = (m_chunkedWriters[fileSocket].writtenToOS >= m_chunkedWriters[fileSocket].data.size());
                m_chunkedWriters.remove(fileSocket);
            }
            fileSocket->deleteLater();
            // Не считаем ошибкой, если файл уже был полностью передан в ОС
            if (messageId > 0 && !wasComplete) emit messageSendFailed(messageId);
        });
        
        fileSocket->connectToHost(ip, 7777);
        return;
    }

    // 2. Для ТЕКСТА и СИСТЕМНЫХ ACK используем мгновенный основной сокет
    if (!m_clients.contains(ip) || m_clients.value(ip)->state() != QAbstractSocket::ConnectedState) {
        connectToPeer(ip, 7777);
    }

    QTcpSocket* socket = m_clients.value(ip);
    if (socket) {
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        
        QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
        out << (quint32)payload.size();
        block.append(payload);
        
        socket->write(block); // Qt умный: он поместит это в буфер и отправит сразу, как только сокет подключится!
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
        connect(socket, &QTcpSocket::bytesWritten, this, &NetworkWorker::onBytesWritten);
        
        // ЗАЩИТА ОТ ПЕРЕЗАПИСИ (Главный фикс стабильности)
        // Если это параллельный сокет для передачи файла, мы не ломаем им основной канал управления!
        if (!m_clients.contains(ip) || m_clients.value(ip)->state() != QAbstractSocket::ConnectedState) {
            if (m_clients.contains(ip)) {
                m_clients.value(ip)->disconnectFromHost();
                m_clients.value(ip)->deleteLater();
            }
            m_clients.insert(ip, socket);
            emit peerConnected(ip);
        }
    }
}

void NetworkWorker::onReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    // Берем IP прямо из сокета. Это решает критический баг со "скрещенными" соединениями
    // (когда узлы подключаются друг к другу одновременно и старый сокет выпадает из m_clients).
    QString ip = socket->peerAddress().toString();
    if (ip.startsWith("::ffff:")) {
        ip = ip.mid(7);
    }
    if (ip.isEmpty()) return;
    
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
        
        // АРХИТЕКТУРНОЕ ТРЕБОВАНИЕ: Ограничение бинарников
        // Увеличено до 150 МБ для компенсации накладных расходов Base64.
        if (packetSize > 157286400) {
            qWarning() << "Aether: Packet exceeds 150 MB! Disconnecting for safety.";
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
            qWarning() << "Aether: JSON parsing error from " << ip;
        }
    }
}

void NetworkWorker::onBytesWritten(qint64 bytes) {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    
    if (m_chunkedWriters.contains(socket)) {
        auto& writer = m_chunkedWriters[socket];
        writer.writtenToOS += bytes;
        
        if (writer.messageId > 0) {
            emit messageUploadProgress(writer.messageId, (double)writer.writtenToOS / writer.data.size());
        }
        
        // Поддерживаем в буфере сокета не более chunkSize
        qint64 chunkSize = 512 * 1024;
        qint64 unwrittenInSocket = writer.pushedToSocket - writer.writtenToOS;
        
        if (unwrittenInSocket < chunkSize && writer.pushedToSocket < writer.data.size()) {
            qint64 toPush = qMin(chunkSize - unwrittenInSocket, (qint64)(writer.data.size() - writer.pushedToSocket));
            if (toPush > 0) {
                socket->write(writer.data.constData() + writer.pushedToSocket, toPush);
                writer.pushedToSocket += toPush;
            }
        }
        
        if (writer.writtenToOS >= writer.data.size()) {
            socket->disconnectFromHost();
        }
    }
}

void NetworkWorker::onSocketConnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    
    // Также используем "чистый" IP при успешном подключении
    QString ip = m_clients.key(socket);
    if (!ip.isEmpty()) {
        emit peerConnected(ip);
    }
}

void NetworkWorker::onSocketDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    
    // КРИТИЧЕСКИ ВАЖНО: Ищем сокет в хеш-таблице и удаляем по ключу.
    // Раньше здесь была ошибка: мы пытались удалить по socket->peerAddress(),
    // что не работало для сокетов, которые не смогли подключиться.
    const QString ip = m_clients.key(socket);
    if (!ip.isEmpty()) {
        m_clients.remove(ip);
        emit peerDisconnected(ip);
    }
    
    m_buffers.remove(socket); // Обязательно очищаем буфер при отключении
    m_chunkedWriters.remove(socket);
    socket->deleteLater(); // Обязательно освобождаем память асинхронно
}

void NetworkWorker::onSocketError(QAbstractSocket::SocketError socketError) {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        // Прячем частые ошибки подключения (собеседник оффлайн, таймаут, нет сети),
        // чтобы не спамить в консоль каждые 10 секунд при фоновом пинге.
        if (socketError != QAbstractSocket::ConnectionRefusedError &&
            socketError != QAbstractSocket::SocketTimeoutError &&
            socketError != QAbstractSocket::HostNotFoundError &&
            socketError != QAbstractSocket::NetworkError) {
            qWarning() << "Aether Network Error:" << socket->errorString();
        }
        
        // ВАЖНО: Если сокет не смог подключиться, сигнал disconnected() НЕ срабатывает.
        // Очищаем "зомби-сокет", чтобы система могла пытаться переподключиться.
        if (socket->state() != QAbstractSocket::ConnectedState) {
            const QString ip = m_clients.key(socket);
            if (!ip.isEmpty()) {
                m_clients.remove(ip);
                emit peerDisconnected(ip); // ВСЕГДА уведомляем интерфейс о потере связи!
            }
            m_buffers.remove(socket);
            m_chunkedWriters.remove(socket);
            socket->deleteLater();
        }
    }
}