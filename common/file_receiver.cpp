#include "file_receiver.h"

#include <QDateTime>
#include <QDir>
#include <QDataStream>
#include <QTcpSocket>
#include <QtEndian>

FileReceiver::FileReceiver(QTcpSocket *socket, QObject *parent)
    : QObject(parent)
    , m_socket(socket)
{
    // 信号与槽：socket 一有新数据到达就进入状态机解析（带中文注释）
    connect(m_socket, &QTcpSocket::readyRead,
            this, &FileReceiver::onReadyRead);
}

void FileReceiver::onReadyRead()
{
    // 把本次到达的所有字节追加进接收缓冲区。
    // TCP 是字节流：这里拿到的可能是半包、整包甚至多个粘在一起的包，
    // 统一交给状态机按协议结构切分。
    m_buffer += m_socket->readAll();

    // while 循环：缓冲区里如果粘了多个包，一次 readyRead 全部处理完
    while (m_buffer.size() > 0) {
        if (!processBuffer())
            return;   // 协议错误已处理（断开连接）
        if (m_state == State::WaitHeader && m_buffer.size() < Packet::kMetaBytes)
            break;    // 剩余数据不足一个协议头，等待下次 readyRead
        if (m_state != State::WaitHeader && m_buffer.isEmpty())
            break;
    }
}

bool FileReceiver::processBuffer()
{
    switch (m_state) {
    case State::WaitHeader:
        if (m_buffer.size() < Packet::kMetaBytes)
            return true;                      // 半包：继续等
        return tryParseHeader();
    case State::WaitFilename:
        if (m_buffer.size() < m_nameLen)
            return true;
        return tryParseFilename();
    case State::WaitSize:
        if (m_buffer.size() < Packet::kSizeBytes)
            return true;
        return tryParseSize();
    case State::WaitData:
        processDataChunk();                   // 有多少写多少，写完可能刚好完成
        return true;
    }
    return true;
}

bool FileReceiver::tryParseHeader()
{
    // 从缓冲区头部读出 8B 定长字段：type(4B) + nameLen(4B)
    qint32 type = 0;
    qint32 nameLen = 0;
    QDataStream ds(m_buffer);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setVersion(QDataStream::Qt_6_0);
    ds >> type >> nameLen;

    if (nameLen <= 0 || nameLen > Packet::kMaxNameLen) {
        // 非法协议头：视为损坏数据，断开连接防止异常
        emit receiveError(QStringLiteral("协议错误：文件名长度非法(%1)").arg(nameLen));
        m_socket->abort();
        resetState();
        return false;
    }

    m_type    = static_cast<Packet::PacketType>(type);
    m_nameLen = nameLen;
    m_buffer.remove(0, Packet::kMetaBytes);   // 消费掉已解析的字节
    m_state   = State::WaitFilename;
    return true;
}

bool FileReceiver::tryParseFilename()
{
    if (m_buffer.size() < m_nameLen)
        return true;                          // 文件名未到齐，继续等

    // 统一按 UTF-8 解码（与发送端 toUtf8 对应），中文文件名不乱码
    m_fileName = QString::fromUtf8(m_buffer.constData(), int(m_nameLen));
    m_buffer.remove(0, int(m_nameLen));
    m_state     = State::WaitSize;
    return true;
}

bool FileReceiver::tryParseSize()
{
    if (m_buffer.size() < Packet::kSizeBytes)
        return true;

    const char *p = m_buffer.constData();
    m_fileSize  = qFromLittleEndian<qint64>(p);
    m_buffer.remove(0, Packet::kSizeBytes);

    if (m_fileSize < 0) {
        emit receiveError(QStringLiteral("协议错误：文件大小非法"));
        m_socket->abort();
        resetState();
        return false;
    }

    // 打开临时落盘文件（目标目录 + 临时名）
    const QString savePath = uniqueSavePath(m_fileName);
    m_outFile.setFileName(savePath + QStringLiteral(".part"));
    if (!m_outFile.open(QIODevice::WriteOnly)) {
        emit receiveError(QStringLiteral("无法创建文件: %1").arg(savePath));
        resetState();
        return false;
    }

    m_gotBytes = 0;
    m_state    = State::WaitData;
    emit receiveStarted(m_fileName, m_fileSize, m_type);
    return true;
}

void FileReceiver::processDataChunk()
{
    // 把缓冲区里已到达的数据尽量写入文件（单次限制一块大小）
    const qint64 canWrite = qMin<qint64>(m_buffer.size(),
                                         m_fileSize - m_gotBytes);
    if (canWrite <= 0)
        return;

    const qint64 written = m_outFile.write(m_buffer.constData(), canWrite);
    if (written < 0) {
        emit receiveError(QStringLiteral("写入文件失败: %1").arg(m_outFile.fileName()));
        m_outFile.close();
        m_outFile.remove();
        resetState();
        return;
    }

    m_gotBytes += written;
    m_buffer.remove(0, int(written));
    emit progress(m_gotBytes, m_fileSize);

    if (m_gotBytes >= m_fileSize)
        finishFile();
}

void FileReceiver::finishFile()
{
    m_outFile.close();

    // 临时文件改名为最终文件名（保证半成品不会顶掉同名旧文件）
    const QString finalPath = uniqueSavePath(m_fileName);
    QFile::remove(finalPath);
    if (!m_outFile.rename(finalPath)) {
        emit receiveError(QStringLiteral("保存文件失败: %1").arg(finalPath));
    } else {
        emit receiveFinished(m_fileName, finalPath, m_type);
    }
    resetState();
}

void FileReceiver::abortReceiving()
{
    if (m_outFile.isOpen()) {
        m_outFile.close();
        m_outFile.remove();   // 删除未完成的 .part 半成品
    }
    resetState();
    m_buffer.clear();
}

void FileReceiver::resetState()
{
    m_state    = State::WaitHeader;
    m_fileName.clear();
    m_nameLen  = 0;
    m_fileSize = 0;
    m_gotBytes = 0;
}

QString FileReceiver::uniqueSavePath(const QString &fileName) const
{
    // 保存路径 = 设置的目录 + 原文件名；重名自动追加 (1)(2)...
    QDir dir(m_saveDir.isEmpty() ? QStringLiteral(".") : m_saveDir);
    if (!dir.exists())
        dir.mkpath(QStringLiteral("."));

    const QFileInfo info(fileName);
    const QString base = info.completeBaseName();
    const QString ext  = info.suffix();
    QString path = dir.filePath(fileName);
    int idx = 1;
    while (QFileInfo::exists(path)) {
        path = dir.filePath(QStringLiteral("%1(%2).%3").arg(base).arg(idx).arg(ext));
        ++idx;
    }
    return path;
}
