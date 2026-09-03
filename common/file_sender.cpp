#include "file_sender.h"

#include <QDataStream>
#include <QFileInfo>
#include <QTcpSocket>

FileSender::FileSender(QTcpSocket *socket, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
{
    // 信号与槽连接（带中文注释）：
    // socket 每成功写出一段数据就触发 bytesWritten，我们在槽里补发下一块，
    // 形成“写一块 → 等确认 → 再写一块”的流水线，避免一次写入过大内存。
    connect(m_socket, &QTcpSocket::bytesWritten,
            this, &FileSender::onBytesWritten);
}

bool FileSender::startSend(const QString &filePath, Packet::PacketType type)
{
    if (isSending()) {
        emit sendError(QStringLiteral("已有文件正在发送，请稍后再试"));
        return false;
    }

    m_file.setFileName(filePath);
    if (!m_file.open(QIODevice::ReadOnly)) {
        // 文件打开失败：通过信号上抛给界面层提示，保证不崩溃
        emit sendError(QStringLiteral("无法打开文件: %1").arg(filePath));
        return false;
    }

    m_fileName = QFileInfo(filePath).fileName(); // 只传文件名，不传路径
    m_fileSize = m_file.size();

    // 发送协议头：消息类型 + 文件名长度 + 文件名 + 文件大小
    m_header     = Packet::makeHeader(type, m_fileName, m_fileSize);
    m_headerLen  = m_header.size();
    m_dataQueued = 0;
    m_acked      = 0;
    m_unacked    = 0;
    m_pumping    = false;

    // 开始流水线发送（协议头 + 首批数据块）
    pump();
    return true;
}

void FileSender::onBytesWritten(qint64 bytes)
{
    if (!isSending()) {
        m_unacked = 0;   // 出错收尾后忽略剩余的写出事件
        return;
    }

    m_acked   += bytes;
    m_unacked -= bytes;

    if (m_pumping)
        return;   // 同步重入保护：pump() 执行期间只做计数

    // 进度 = 已确认写出的文件数据字节（扣除协议头）
    const qint64 sentData = qBound<qint64>(0, m_acked - m_headerLen, m_fileSize);
    emit progress(sentData, m_fileSize);

    // 完成条件：协议头 + 全部文件数据都已写出
    if (m_acked >= m_headerLen + m_fileSize)
        finish();
    else
        pump();   // 窗口有空位，继续补充发送
}

/**
 * 流水线补充：只要“在途字节”小于窗口就继续 write 下一块。
 * 注意：write() 在本地缓冲未满时会【同步】触发 onBytesWritten，
 * 用 m_pumping 防止重入导致一次性读穿文件。
 */
void FileSender::pump()
{
    m_pumping = true;
    while (m_unacked < Packet::kWindowSize) {
        // 注意：剩余量必须用“已入队数据字节数”推算，
        // 不能用 m_acked（同步 ack 未到时 m_acked 滞后，会把 remain 算大，
        // 导致在 EOF 处读到空数据而误判为读取异常）。
        const qint64 remain = m_fileSize - m_dataQueued;
        if (remain <= 0)
            break;   // 文件数据已全部 write 完，只等剩余 ack

        QByteArray chunk;
        if (!m_header.isEmpty()) {
            chunk += m_header;      // 首次：协议头与首个数据块合并写出
            m_header.clear();
        }
        // 读一块数据（最后一块允许不足 kChunkSize，读到 EOF 是正常的）
        const QByteArray data = m_file.read(qMin<qint64>(Packet::kChunkSize, remain));
        if (data.isEmpty()) {
            // remain > 0 却读不到数据：文件读取异常，报错兜底
            m_pumping = false;
            emit sendError(QStringLiteral("文件读取异常: %1").arg(m_fileName));
            m_file.close();
            return;
        }
        m_dataQueued += data.size();
        chunk += data;

        m_unacked += chunk.size();
        m_socket->write(chunk);
        // 若 write 同步触发了 onBytesWritten（回环/小缓冲场景），
        // 计数已在槽里更新，由 m_pumping 保护不重复收尾。
    }

    // 空文件：循环直接 break，协议头还未写出，这里单独补发
    if (!m_header.isEmpty()) {
        m_unacked += m_header.size();
        m_socket->write(m_header);
        m_header.clear();
    }
    m_pumping = false;

    // 极小文件：pump() 期间可能已同步收全 ack（重入保护只计数未收尾），
    // 这里补做完成判断。
    if (m_unacked == 0 && m_acked >= m_headerLen + m_fileSize)
        finish();
}

void FileSender::finish()
{
    if (!m_file.isOpen())
        return;   // 幂等保护：防止重复发 sendFinished
    m_file.close();
    m_header.clear();
    m_unacked = 0;
    emit sendFinished(m_fileName, m_fileSize);
}
