#ifndef FILE_RECEIVER_H
#define FILE_RECEIVER_H

#include <QObject>
#include <QByteArray>
#include <QFile>
#include <QString>

#include "packet.h"

class QTcpSocket;

/**
 * @brief 文件接收器 —— 在 TCP 字节流上按自定义协议切包、组包、落盘
 *
 * 解决粘包/半包的核心：维护接收缓冲区 m_buffer，用状态机循环解析：
 *
 *   WAIT_HEADER(8B: type+nameLen)
 *     → WAIT_FILENAME(nameLen B)
 *       → WAIT_SIZE(8B: fileSize)
 *         → WAIT_DATA(fileSize B，分多次到达也正确拼接)
 *           → 完成，回到 WAIT_HEADER（一包接一包处理）
 *
 * 文件数据不额外复制：直接把 m_buffer 中已到的部分分块写入临时文件，
 * 收满后 rename 成目标文件名，避免重名残留半个文件。
 */
class FileReceiver : public QObject
{
    Q_OBJECT
public:
    enum State {
        WaitHeader,   ///< 等待定长字段 type+nameLen（8B）
        WaitFilename, ///< 等待文件名（变长）
        WaitSize,     ///< 等待 fileSize（8B）
        WaitData      ///< 等待文件数据
    };
    Q_ENUM(State)

    explicit FileReceiver(QTcpSocket *socket, QObject *parent = nullptr);

    /** @brief 设置接收文件的保存目录（由设置对话框提供） */
    void setSaveDir(const QString &dir) { m_saveDir = dir; }
    QString saveDir() const { return m_saveDir; }

    /** @brief 是否正在接收文件 */
    bool isReceiving() const { return m_state == State::WaitData; }

    /** @brief 中止当前接收：关闭并删除未完成的 .part 临时文件，复位状态机 */
    void abortReceiving();

signals:
    /** 开始接收一个文件（用于日志展示文件名与大小） */
    void receiveStarted(const QString &fileName, qint64 fileSize,
                        Packet::PacketType type);
    /** 接收进度（驱动 QProgressBar） */
    void progress(qint64 received, qint64 total);
    /** 单个文件接收完成，savedPath 为实际保存的完整路径 */
    void receiveFinished(const QString &fileName, const QString &savedPath,
                         Packet::PacketType type);
    /** 接收出错 */
    void receiveError(const QString &message);

private slots:
    /** readyRead 的处理槽：状态机解析 m_buffer */
    void onReadyRead();

private:
    bool processBuffer();                 ///< 循环解析缓冲区，返回 false 表示协议错误
    bool tryParseHeader();                ///< 各状态下的解析步骤
    bool tryParseFilename();
    bool tryParseSize();
    void processDataChunk();              ///< 把已到达的数据写入文件
    void finishFile();                    /// 收满 fileSize，收尾落盘
    void resetState();                    ///< 收尾/出错后复位状态机
    QString uniqueSavePath(const QString &fileName) const; ///< 重名自动加 (n)

    QTcpSocket *m_socket  = nullptr;
    QByteArray  m_buffer;                 ///< 接收缓冲区（组包核心）
    State       m_state     = State::WaitHeader;

    Packet::PacketType m_type   = Packet::PacketType::TextFile;
    QString     m_fileName;               ///< 当前文件名
    qint64      m_nameLen   = 0;          ///< 协议头中的文件名长度
    qint64      m_fileSize  = 0;          ///< 协议头中的文件总大小
    qint64      m_gotBytes  = 0;          ///< 已接收的数据字节数
    QFile       m_outFile;                ///< 落盘的临时文件
    QString     m_saveDir;                ///< 保存目录
};

#endif // FILE_RECEIVER_H
