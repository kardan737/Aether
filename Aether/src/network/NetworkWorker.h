#pragma once
#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QHash>
#include <QString>
#include <QJsonObject>
#include <QByteArray>

class NetworkWorker : public QObject {
    Q_OBJECT
public:
    explicit NetworkWorker(QObject *parent = nullptr);
    ~NetworkWorker();

public slots:
    // Запуск P2P сервера для ожидания подключений
    void startServer(quint16 port);
    
    // Создание туннеля к другому узлу
    void connectToPeer(const QString& ip, quint16 port);
    
    // Отправка сообщения в формате JSON
    void sendJsonMessage(int messageId, const QString& ip, const QJsonObject& json);

signals:
    void serverStarted(bool success, const QString& message);
    void peerConnected(const QString& ip);
    void peerDisconnected(const QString& ip);
    void messageReceived(const QString& ip, const QJsonObject& json);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onSocketDisconnected();
    void onSocketConnected();
    void onSocketError(QAbstractSocket::SocketError socketError);

private:
    QTcpServer* m_server;
    // Храним активные сокеты. Ключ - IP адрес собеседника
    QHash<QString, QTcpSocket*> m_clients;

    // Буферы для склейки разорванных TCP-пакетов
    QHash<QTcpSocket*, QByteArray> m_buffers;
};