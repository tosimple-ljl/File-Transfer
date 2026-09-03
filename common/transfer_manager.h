#ifndef TRANSFER_MANAGER_H
#define TRANSFER_MANAGER_H

#include <QObject>
#include <QString>

#include "packet.h"

class QTcpSocket;
class FileSender;
class FileReceiver;

/**
 * @brief 传输会话管理器 —— 把一条 socket 连接与其收发器绑定
 *
 * 一个连接 = 一个 TransferManager。
 * 职责：
 *   - 转发连接状态信号（connected/disconnected/error）给界面层；
 *   - 提供发送入口（内部委托 FileSender）；
 *   - 转发接收进度/完成信号（内部委托 FileReceiver）；
 *   - 连接断开时清理资源（包括删除未完成的 .part 临时文件）。
 *
 * 界面层（MainWindow）只与本类交互，不直接操作 socket，
 * 实现 UI 与网络逻辑的适度分离。
 */
class TransferManager : public QObject
{
    Q_OBJECT
public:
    explicit TransferManager(QTcpSocket *socket, QObject *parent = nullptr);
    ~TransferManager() override;

    QTcpSocket *socket() const { return m_socket; }

    /** 对端地址:端口 描述（用于日志） */
    QString peerDescription() const;

    /** @brief 设置接收文件的保存目录 */
    void setSaveDir(const QString &dir);

    /** @brief 发送文件（返回 false 表示未能启动，如正在发送中） */
    bool sendFile(const QString &filePath, Packet::PacketType type);

    /** @brief 当前是否正在发送文件 */
    bool isSending() const;

    /** @brief 当前是否正在接收文件 */
    bool isReceiving() const;

signals:
    // ---- 连接状态（转发 socket 信号） ----
    void peerDisconnected(const QString &peer);           ///< 对端断开
    void connectionError(const QString &message);          ///< socket 错误

    // ---- 发送侧 ----
    void sendProgress(qint64 sent, qint64 total);
    void sendFinished(const QString &fileName, qint64 fileSize);
    void sendError(const QString &message);

    // ---- 接收侧 ----
    void receiveStarted(const QString &fileName, qint64 fileSize,
                        Packet::PacketType type);
    void receiveProgress(qint64 received, qint64 total);
    void receiveFinished(const QString &fileName, const QString &savedPath,
                         Packet::PacketType type);
    void receiveError(const QString &message);

private:
    void cleanupPartialFile();               ///< 删除未完成的 .part 文件

    QTcpSocket    *m_socket    = nullptr;
    FileSender    *m_sender    = nullptr;
    FileReceiver  *m_receiver  = nullptr;
};

#endif // TRANSFER_MANAGER_H
