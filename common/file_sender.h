#ifndef FILE_SENDER_H
#define FILE_SENDER_H

#include <QObject>
#include <QFile>
#include <QString>

#include "packet.h"

class QTcpSocket;

/**
 * @brief 文件发送器 —— 采用 write + bytesWritten 信号驱动的流水线发送
 *
 * 不把整个文件读入内存：每次只写入一块（kChunkSize），
 * 等 socket 通知“上一块已写完”(bytesWritten) 再写下一块，
 * 内存占用恒定，且天然避免一次性 write 过大导致 UI 卡顿。
 *
 * 协议：先发协议头（见 packet.h），随后流水线发送文件数据。
 *
 * 可靠性细节：QTcpSocket::write() 在本地写缓冲未满时会【同步】发出
 * bytesWritten 信号（尤其回环/小文件场景），若不防重入会一次读穿整个
 * 文件并造成事件重入，因此用 m_pumping 标志保护。
 */
class FileSender : public QObject
{
    Q_OBJECT
public:
    explicit FileSender(QTcpSocket *socket, QObject *parent = nullptr);

    /**
     * @brief 开始发送一个文件
     * @param filePath 本地文件路径
     * @param type     传输类型（文本/图片）
     * @return 成功启动返回 true；文件打不开返回 false
     */
    bool startSend(const QString &filePath, Packet::PacketType type);

    /** @brief 是否正在发送 */
    bool isSending() const { return m_file.isOpen(); }

    /** @brief 正在发送的文件名（未在发送时返回空串） */
    QString currentFileName() const { return m_fileName; }

signals:
    /** 传输进度（驱动 QProgressBar） */
    void progress(qint64 sent, qint64 total);
    /** 单个文件发送完成 */
    void sendFinished(const QString &fileName, qint64 fileSize);
    /** 发送出错（文件打不开、写 socket 失败等） */
    void sendError(const QString &message);

private slots:
    /** 流水线核心：socket 每写完一段数据就补充发送 */
    void onBytesWritten(qint64 bytes);

private:
    void pump();     ///< 补充发送：保持“在途数据”不超过窗口大小
    void finish();   ///< 收尾：关文件、发完成信号

    QTcpSocket *m_socket = nullptr;
    QFile       m_file;               ///< 待发送文件句柄
    QString     m_fileName;           ///< 仅文件名（写入协议头）
    qint64      m_fileSize  = 0;      ///< 文件总大小
    qint64      m_headerLen = 0;      ///< 协议头长度（ack 统计要扣除）
    QByteArray  m_header;             ///< 待写出的协议头（写出后清空）
    qint64      m_dataQueued = 0;     ///< 已从文件读出并入队的数据字节数
    qint64      m_acked    = 0;       ///< socket 已确认写出的总字节（含协议头）
    qint64      m_unacked  = 0;       ///< 已 write 但尚未确认的字节（在途）
    bool        m_pumping  = false;   ///< 防止 bytesWritten 同步重入
};

#endif // FILE_SENDER_H
