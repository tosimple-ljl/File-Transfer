#include "transfer_manager.h"

#include "file_sender.h"
#include "file_receiver.h"

#include <QTcpSocket>

TransferManager::TransferManager(QTcpSocket *socket, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
{
    m_sender   = new FileSender(m_socket, this);
    m_receiver = new FileReceiver(m_socket, this);

    // ---- 转发发送侧信号（Model -> View 通过信号上抛） ----
    connect(m_sender, &FileSender::progress,
            this, &TransferManager::sendProgress);
    connect(m_sender, &FileSender::sendFinished,
            this, &TransferManager::sendFinished);
    connect(m_sender, &FileSender::sendError,
            this, &TransferManager::sendError);

    // ---- 转发接收侧信号 ----
    connect(m_receiver, &FileReceiver::receiveStarted,
            this, &TransferManager::receiveStarted);
    connect(m_receiver, &FileReceiver::progress,
            this, &TransferManager::receiveProgress);
    connect(m_receiver, &FileReceiver::receiveFinished,
            this, &TransferManager::receiveFinished);
    connect(m_receiver, &FileReceiver::receiveError,
            this, &TransferManager::receiveError);

    // ---- 连接状态信号与槽连接（带中文注释） ----
    // 对端断开：清理会话（含未完成临时文件），并通知界面
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        cleanupPartialFile();
        emit peerDisconnected(peerDescription());
    });

    // socket 层错误（连接被拒、网络不可达、超时等）上抛给界面提示
    connect(m_socket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        emit connectionError(m_socket->errorString());
    });
}

TransferManager::~TransferManager()
{
    cleanupPartialFile();
}

QString TransferManager::peerDescription() const
{
    return QStringLiteral("%1:%2")
        .arg(m_socket->peerAddress().toString())
        .arg(m_socket->peerPort());
}

void TransferManager::setSaveDir(const QString &dir)
{
    m_receiver->setSaveDir(dir);
}

bool TransferManager::sendFile(const QString &filePath, Packet::PacketType type)
{
    return m_sender->startSend(filePath, type);
}

bool TransferManager::isSending() const
{
    return m_sender->isSending();
}

bool TransferManager::isReceiving() const
{
    return m_receiver->isReceiving();
}

void TransferManager::cleanupPartialFile()
{
    // 若正在接收中连接断开/销毁，通知接收器丢弃半成品临时文件，
    // 避免在保存目录残留损坏的 .part 文件
    m_receiver->abortReceiving();
}
