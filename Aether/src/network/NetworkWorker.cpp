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
        return;
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
    if (!m_clients.contains(ip)) {
        // Сокета нет? Значит, пытаемся переподключиться к узлу!
        // Сообщение пока не отправляем, оно уйдет в следующий тик таймера.
        connectToPeer(ip, 7777);
        if (messageId > 0) emit messageSendFailed(messageId);
        return;
    }

    QTcpSocket* socket = m_clients.value(ip);
    
    // --- АНТИ-БЛОКИРОВКА (Fast-Lane) ---
    // Если сокет забит передачей тяжелого файла (> 1 МБ), а мы шлем срочный текст или системный ACK
    if (socket->state() == QAbstractSocket::ConnectedState && socket->bytesToWrite() > 1024 * 1024) {
        if (json.value("type").toString() != "file") {
            qDebug() << "Aether P2P: Fast-lane activated! Opening temporary socket for urgent message to" << ip;
            QTcpSocket* fastSocket = new QTcpSocket(this);
            
            connect(fastSocket, &QTcpSocket::connected, this, [fastSocket, json]() {
                QByteArray block;
                QDataStream out(&block, QIODevice::WriteOnly);
                out.setVersion(QDataStream::Qt_6_0);
                QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
                out << (quint32)payload.size();
                block.append(payload);
                fastSocket->write(block);
            });
            
            // Как только отправит пакет - отключаемся
            connect(fastSocket, &QTcpSocket::bytesWritten, this, [fastSocket](qint64 bytes) {
                if (fastSocket->bytesToWrite() == 0) {
                    fastSocket->disconnectFromHost();
                }
            });
            
            // Надежно очищаем память при отключении или ошибке
            connect(fastSocket, &QTcpSocket::disconnected, fastSocket, &QTcpSocket::deleteLater);
            connect(fastSocket, &QTcpSocket::errorOccurred, fastSocket, &QTcpSocket::deleteLater);
            
            fastSocket->connectToHost(ip, 7777);
            return; // Выходим, сообщение улетит по параллельному быстрому каналу
        }
    }
    // ------------------------------------

    if (socket->state() == QAbstractSocket::ConnectedState) {
        QByteArray block;
        QDataStream out(&block, QIODevice::WriteOnly);
        out.setVersion(QDataStream::Qt_6_0);
        
        QByteArray payload = QJsonDocument(json).toJson(QJsonDocument::Compact);
        out << (quint32)payload.size(); // Сначала записываем размер пакета (4 байта)
        block.append(payload);          // Затем сам JSON Payload
        
        m_pendingWrites[socket].append({messageId, block.size(), 0});
        if (messageId > 0) {
            emit messageUploadProgress(messageId, 0.0); // Явно сбрасываем прогресс в 0 перед началом отправки
        }
        socket->write(block);
        // Мы больше не ставим галочки здесь! Ждем "ack" от собеседника.
    } else {
        if (messageId > 0) emit messageSendFailed(messageId);
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
        
        m_clients.insert(ip, socket);
        emit peerConnected(ip);
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
    if (!socket || !m_pendingWrites.contains(socket)) return;
    
    auto& queue = m_pendingWrites[socket];
    while (bytes > 0 && !queue.isEmpty()) {
        auto& front = queue.first();
        qint64 remaining = front.totalBytes - front.writtenBytes;
        if (bytes >= remaining) {
            bytes -= remaining;
            emit messageUploadProgress(front.messageId, 1.0); // 100%
            queue.removeFirst();
        } else {
            front.writtenBytes += bytes;
            emit messageUploadProgress(front.messageId, (double)front.writtenBytes / front.totalBytes);
            break;
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
    m_pendingWrites.remove(socket);
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
            if (!ip.isEmpty()) m_clients.remove(ip);
            m_buffers.remove(socket);
            m_pendingWrites.remove(socket);
            socket->deleteLater();
        }
    }
}