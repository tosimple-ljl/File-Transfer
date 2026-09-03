#ifndef PACKET_H
#define PACKET_H

#include <QByteArray>
#include <QMetaType>
#include <QString>

/**
 * @brief 自定义应用层协议（解决 TCP 粘包/半包问题的核心）
 *
 * 协议头 + 载荷格式（整数统一小端序）：
 * ------------------------------------------------------------------
 * | 消息类型(4B) | 文件名长度(4B) | 文件名(变长 UTF-8) | 文件大小(8B) | 文件数据(变长) |
 * ------------------------------------------------------------------
 *
 * 接收端依据该结构在字节流中切分包（见 FileReceiver 的状态机）。
 */
namespace Packet {

/** 消息类型枚举 */
enum class PacketType : qint32 {
    TextFile  = 0x0001,   ///< 文本文件
    ImageFile = 0x0002    ///< 图片文件
};

/** 协议头中定长字段：type(4B) + nameLen(4B) */
constexpr qint32 kMetaBytes   = 8;
/** 协议头中 fileSize 字段：8B */
constexpr qint32 kSizeBytes   = 8;
/** 发送块大小：单次 write 的数据量（避免大文件占过多内存） */
constexpr qint32 kChunkSize   = 64 * 1024;
/** 发送窗口：socket 写缓冲中允许的最大在途数据量（字节） */
constexpr qint32 kWindowSize  = 4 * 64 * 1024;
/** 文件名最大长度（防止非法协议头导致内存爆炸） */
constexpr qint32 kMaxNameLen  = 1 << 20;   // 1MB

/**
 * @brief 序列化协议头：type + nameLen + 文件名 + fileSize
 * @return 组装好的协议头字节数组（不含文件数据）
 */
QByteArray makeHeader(PacketType type, const QString &fileName, qint64 fileSize);

/** @brief 根据文件扩展名判断传输类型（文本 or 图片） */
PacketType typeForFile(const QString &filePath);

/** @brief 类型对应的中文描述（用于日志） */
QString typeToString(PacketType type);

} // namespace Packet

#endif // PACKET_H
