#include "packet.h"

#include <QDataStream>
#include <QFileInfo>
#include <QtEndian>

namespace Packet {

QByteArray makeHeader(PacketType type, const QString &fileName, qint64 fileSize)
{
    // 文件名统一使用 UTF-8 编码，保证中文名跨平台不乱码
    const QByteArray name = fileName.toUtf8();

    QByteArray out;
    out.reserve(kMetaBytes + name.size() + kSizeBytes);

    // 用 QDataStream 写入定长字段，显式指定小端序与 Qt6 版本
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds.setVersion(QDataStream::Qt_6_0);
    ds << static_cast<qint32>(type)      // 消息类型 4B
       << static_cast<qint32>(name.size()); // 文件名长度 4B

    out.append(name);                    // 文件名（变长）

    // fileSize 用 qToLittleEndian 手动追加，避免再建一个流
    char sizeBuf[8];
    qToLittleEndian<qint64>(fileSize, sizeBuf);
    out.append(sizeBuf, kSizeBytes);

    return out;
}

PacketType typeForFile(const QString &filePath)
{
    static const QStringList kImageExts = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("bmp"), QStringLiteral("gif"), QStringLiteral("webp")
    };
    const QString ext = QFileInfo(filePath).suffix().toLower();
    return kImageExts.contains(ext) ? PacketType::ImageFile : PacketType::TextFile;
}

QString typeToString(PacketType type)
{
    switch (type) {
    case PacketType::ImageFile: return QStringLiteral("图片文件");
    case PacketType::TextFile:  return QStringLiteral("文本文件");
    }
    return QStringLiteral("未知类型");
}

} // namespace Packet
