/**
 * @brief 核心逻辑端到端自测（无 GUI）
 *
 * 在同一进程内启动 QTcpServer + QTcpSocket 回环连接，
 * 用 common 里的 FileSender / FileReceiver 传输：
 *   1) 含中文的文本文件
 *   2) 8MB 随机二进制（模拟图片等大文件，必然触发多次分包）
 * 然后逐字节比对收发文件是否一致。
 */
#include "file_sender.h"
#include "file_receiver.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRandomGenerator>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <cstdio>

static int  g_failCount = 0;
static int  g_done = 0;               // 已完成的传输个数
static const int kTotalTransfers = 2;

/** 逐字节比较两个文件内容 */
static bool filesEqual(const QString &a, const QString &b, QString *err)
{
    QFile fa(a), fb(b);
    if (!fa.open(QIODevice::ReadOnly) || !fb.open(QIODevice::ReadOnly)) {
        *err = QStringLiteral("打开文件失败");
        return false;
    }
    if (fa.size() != fb.size()) {
        *err = QStringLiteral("大小不一致: %1 vs %2").arg(fa.size()).arg(fb.size());
        return false;
    }
    const qint64 total = fa.size();
    qint64 offset = 0;
    while (offset < total) {
        const QByteArray da = fa.read(64 * 1024);
        const QByteArray db = fb.read(64 * 1024);
        if (da != db) {
            *err = QStringLiteral("第 %1 字节附近内容不一致").arg(offset);
            return false;
        }
        offset += da.size();
    }
    return true;
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // ---- 准备测试文件 ----
    const QString sendDir = QDir::currentPath() + QStringLiteral("/test_src");
    const QString recvDir = QDir::currentPath() + QStringLiteral("/test_recv");
    QDir().mkpath(sendDir);
    QDir().mkpath(recvDir);

    // 1) 含中文的文本文件
    const QString textPath = sendDir + QStringLiteral("/中文测试.txt");
    {
        QFile f(textPath);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        for (int i = 0; i < 5000; ++i)
            f.write(QStringLiteral("第%1行：文件传输工具测试 ABCabc123！\n").arg(i).toUtf8());
    }
    // 2) 8MB 随机二进制（模拟图片）
    const QString binPath = sendDir + QStringLiteral("/photo_随机.png");
    {
        QFile f(binPath);
        f.open(QIODevice::WriteOnly | QIODevice::Truncate);
        QRandomGenerator rng(12345);
        for (int i = 0; i < 8 * 1024; ++i) {   // 8MB，按 1KB 随机块写入
            QByteArray blk(1024, Qt::Uninitialized);
            for (int j = 0; j < blk.size(); ++j)
                blk[j] = char(rng.generate() & 0xFF);
            f.write(blk);
        }
    }

    // ---- 建立回环连接 ----
    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0)) {
        qWarning("服务端监听失败: %s", qPrintable(server.errorString()));
        return 1;
    }
    auto *clientSock = new QTcpSocket(&app);
    clientSock->connectToHost(QHostAddress::LocalHost, server.serverPort());
    if (!clientSock->waitForConnected(3000)) {
        qWarning("客户端连接失败");
        return 1;
    }
    // 等待服务端处理完连接（需要事件处理，waitForNewConnection 可在 exec 前用）
    if (!server.waitForNewConnection(3000)) {
        qWarning("服务端无待处理连接: %s", qPrintable(server.errorString()));
        return 1;
    }
    auto *serverSock = server.nextPendingConnection();
    if (!serverSock) {
        qWarning("nextPendingConnection 返回空");
        return 1;
    }

    // ---- 服务端：FileReceiver；客户端：FileSender（与 GUI 完全同一套代码） ----
    auto *receiver = new FileReceiver(serverSock, &app);
    receiver->setSaveDir(recvDir);

    auto *sender = new FileSender(clientSock, &app);

    const QStringList testFiles = { textPath, binPath };

    QObject::connect(receiver, &FileReceiver::receiveFinished,
        [&](const QString &name, const QString &savedPath, Packet::PacketType type) {
            QString err;
            const QString src = QDir(sendDir).filePath(name);
            const bool same = filesEqual(src, savedPath, &err);
            qInfo("[%d/%d] %s: %s (%s) -> %s  [%s]",
                  ++g_done, kTotalTransfers,
                  Packet::typeToString(type).toUtf8().constData(),
                  name.toUtf8().constData(),
                  QFileInfo(savedPath).size() == QFileInfo(src).size() ? "大小OK" : "大小FAIL",
                  savedPath.toUtf8().constData(),
                  same ? "内容一致 PASS" : qPrintable(QStringLiteral("FAIL: ") + err));
            if (!same) ++g_failCount;

            // 全部完成 → 退出
            if (g_done == kTotalTransfers) {
                qInfo("==== 测试结束: %s ====", g_failCount == 0 ? "全部通过" : "存在失败");
                app.exit(g_failCount == 0 ? 0 : 1);
            }
        });
    QObject::connect(receiver, &FileReceiver::receiveError,
        [&](const QString &msg) { qWarning("接收错误: %s", qPrintable(msg)); ++g_failCount; });
    QObject::connect(sender, &FileSender::sendError,
        [&](const QString &msg) { qWarning("发送错误: %s", qPrintable(msg)); ++g_failCount; });

    // 依次发送两个文件（第一个发完再发第二个）。
    // 注意用 0 延时把下一次 startSend 移出当前 bytesWritten 信号栈，
    // 避免上一个文件的剩余 ack 事件混入下一个文件的计数。
    int idx = 0;
    QObject::connect(sender, &FileSender::sendFinished, [&](const QString &) {
        ++idx;
        if (idx < testFiles.size()) {
            const QString next = testFiles[idx];
            QTimer::singleShot(0, [&sender, next, &idx] {
                sender->startSend(next, Packet::typeForFile(next));
            });
        }
    });
    // 稍等事件循环启动后开始发送
    QTimer::singleShot(100, [&] {
        sender->startSend(testFiles[0], Packet::typeForFile(testFiles[0]));
    });

    // 超时保护：30 秒未完成视为失败
    QTimer::singleShot(30000, [&] {
        qWarning("测试超时！完成 %d/%d", g_done, kTotalTransfers);
        app.exit(2);
    });

    return app.exec();
}
