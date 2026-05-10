#pragma once
#include <QAbstractListModel>
#include <QString>
#include <QList>
#include <QMetaType>

struct MessageData {
    int id;
    QString text;
    bool isMine;
    int status;
    QString time;
    double uploadProgress = 0.0;
};
Q_DECLARE_METATYPE(MessageData)
Q_DECLARE_METATYPE(QList<MessageData>)

class MessagesModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TextRole,
        IsMineRole,
        StatusRole,
        TimeRole,
        UploadProgressRole
    };

    explicit MessagesModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

public slots:
    void setMessages(const QList<MessageData>& messages);
    void appendMessage(const MessageData& message);
    void clear();
    void updateMessageStatus(int messageId, int status);
    void updateMessageProgress(int messageId, double progress);

private:
    QList<MessageData> m_messages;
};