#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "settingsdialog.h"
#include "transfer_manager.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHostAddress>
#include <QMessageBox>
#include <QMimeData>
#include <QStyle>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>
#include <QDateTime>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // QTcpServer：服务端监听 socket
    m_server = new QTcpServer(this);

    setupUiExtras();

    // ================ 信号与槽连接（带中文注释） ================
    // 1) 按钮
    connect(ui->btnStart,     &QPushButton::clicked, this, &MainWindow::onStartClicked);
    connect(ui->btnStop,      &QPushButton::clicked, this, &MainWindow::onStopClicked);
    connect(ui->btnSettings,  &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    connect(ui->btnOpenFile,  &QPushButton::clicked, this, &MainWindow::onOpenFileClicked);

    // 2) 菜单
    connect(ui->actionSettings, &QAction::triggered, this, &MainWindow::onSettingsClicked);
    connect(ui->actionQuit,     &QAction::triggered, this, &MainWindow::close);
    connect(ui->actionAbout,    &QAction::triggered, this, [this] {
        QMessageBox::information(this, QStringLiteral("关于"),
            QStringLiteral("基于 Qt TCP Socket 的文件传输工具\n"
                           "课程作业 - 服务端\n支持文本/图片文件可靠传输"));
    });

    // 3) QTcpServer::newConnection：有客户端连进来
    //    在槽里 nextPendingConnection() 取出 socket 并建立传输会话
    connect(m_server, &QTcpServer::newConnection, this, &MainWindow::onNewConnection);
    // 监听失败等错误提示
    connect(m_server, &QTcpServer::acceptError, this, [this](QAbstractSocket::SocketError) {
        appendLog(QStringLiteral("[错误] 接受连接失败: %1").arg(m_server->errorString()));
    });

    updateButtonStates();
    statusBar()->showMessage(QStringLiteral("就绪。请先在“服务设置”中配置监听端口。"));
}

MainWindow::~MainWindow()
{
    // 析构时优雅关闭监听并断开所有客户端
    if (m_server->isListening())
        m_server->close();
    qDeleteAll(m_sessions);
    delete ui;
}

void MainWindow::setupUiExtras()
{
    // ---- 拖拽接收区：接受拖拽 + 事件过滤器（与客户端一致） ----
    ui->dropArea->setAcceptDrops(true);
    ui->dropArea->installEventFilter(this);
    setAcceptDrops(true);

    ui->logEdit->setMaximumBlockCount(500);
    updateButtonStates();
}

// ==================== 监听控制 ====================

void MainWindow::onStartClicked()
{
    if (m_server->isListening()) {
        appendLog(QStringLiteral("已在监听中，忽略重复操作"));
        return;
    }
    if (m_listenPort == 0)
        m_listenPort = 8888;

    // QTcpServer::listen：开始监听指定端口（任何网卡）
    if (m_server->listen(QHostAddress::Any, m_listenPort)) {
        appendLog(QStringLiteral("开始监听端口 %1 ...").arg(m_listenPort));
        statusBar()->showMessage(QStringLiteral("监听中: 端口 %1").arg(m_listenPort));
    } else {
        // 监听失败（端口被占用等）→ 提示且不崩溃
        QMessageBox::warning(this, QStringLiteral("监听失败"), m_server->errorString());
        appendLog(QStringLiteral("[错误] 监听失败: %1").arg(m_server->errorString()));
    }
    updateButtonStates();
}

void MainWindow::onStopClicked()
{
    if (m_server->isListening()) {
        const quint16 port = m_server->serverPort();
        m_server->close();
        appendLog(QStringLiteral("已停止监听端口 %1").arg(port));
    }
    updateButtonStates();
}

void MainWindow::onNewConnection()
{
    // 可能有多个待处理连接，循环取干净
    while (m_server->hasPendingConnections()) {
        QTcpSocket *socket = m_server->nextPendingConnection();
        if (!socket)
            break;

        // 每个客户端连接 = 一个 TransferManager 会话
        auto *session = new TransferManager(socket, this);
        session->setSaveDir(m_saveDir);
        m_sessions.append(session);

        // 会话结束（对端断开）→ 清理
        connect(session, &TransferManager::peerDisconnected, this, [this, session](const QString &peer) {
            appendLog(QStringLiteral("客户端断开: %1").arg(peer));
            m_sessions.removeAll(session);
            session->deleteLater();
            updateClientCount();
        });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);

        // 转发该会话的日志/进度信号到界面
        connect(session, &TransferManager::sendProgress,    this, &MainWindow::onSendProgress);
        connect(session, &TransferManager::sendFinished,    this, &MainWindow::onSendFinished);
        connect(session, &TransferManager::sendError,       this, &MainWindow::onTransferError);
        connect(session, &TransferManager::receiveStarted,  this, &MainWindow::onReceiveStarted);
        connect(session, &TransferManager::receiveProgress, this, &MainWindow::onReceiveProgress);
        connect(session, &TransferManager::receiveFinished, this, &MainWindow::onReceiveFinished);
        connect(session, &TransferManager::receiveError,    this, &MainWindow::onTransferError);
        connect(session, &TransferManager::connectionError, this, [this](const QString &msg) {
            appendLog(QStringLiteral("[连接错误] %1").arg(msg));
        });

        appendLog(QStringLiteral("客户端已连接: %1").arg(session->peerDescription()));
        updateClientCount();
    }
}

// ==================== 设置对话框（窗口间通信） ====================

void MainWindow::onSettingsClicked()
{
    if (!m_settingsDlg) {
        m_settingsDlg = new SettingsDialog(this);
        // 跨窗口通信核心：对话框发信号，主窗口槽接收
        connect(m_settingsDlg, &SettingsDialog::settingsConfirmed,
                this, &MainWindow::onSettingsConfirmed);
    }
    m_settingsDlg->hideIpRow();          // 服务端不需要 IP 一栏
    m_settingsDlg->setPort(m_listenPort);
    m_settingsDlg->setSaveDir(m_saveDir);
    m_settingsDlg->show();
    m_settingsDlg->raise();
    m_settingsDlg->activateWindow();
}

void MainWindow::onSettingsConfirmed(const QString &ip, quint16 port, const QString &saveDir)
{
    Q_UNUSED(ip);   // 服务端不使用 IP
    m_listenPort = port;
    m_saveDir    = saveDir;
    appendLog(QStringLiteral("设置已更新: 监听端口=%1 保存目录=%2").arg(port).arg(saveDir));
    statusBar()->showMessage(QStringLiteral("设置已更新"), 3000);
}

// ==================== 发送（服务端也可向客户端发文件） ====================

void MainWindow::onOpenFileClicked()
{
    if (m_sessions.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("当前没有客户端连接"));
        return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("选择要发送的文件"), QString(),
        QStringLiteral("所有文件 (*);;文本文件 (*.txt);;图片文件 (*.png *.jpg *.jpeg *.bmp)"));
    if (files.isEmpty())
        return;

    // 简化处理：发给最近接入的客户端
    TransferManager *target = m_sessions.last();
    for (const QString &f : files) {
        const Packet::PacketType type = Packet::typeForFile(f);
        const QFileInfo info(f);
        appendLog(QStringLiteral("向 %1 发送%2: %3 (%4 字节)")
                      .arg(target->peerDescription(),
                           Packet::typeToString(type), info.fileName())
                      .arg(info.size()));
        if (!target->sendFile(f, type))
            appendLog(QStringLiteral("[错误] 发送失败: %1").arg(info.fileName()));
    }
}

// ==================== 虚函数重写：窗口级拖拽 ====================

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    // 服务端拖入文件 = 向（最近连接的）客户端发送
    if (event->mimeData()->hasUrls() && !m_sessions.isEmpty()) {
        event->acceptProposedAction();
        setDragHighlight(true);
    }
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    setDragHighlight(false);
    event->accept();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    setDragHighlight(false);
    if (m_sessions.isEmpty())
        return;
    TransferManager *target = m_sessions.last();
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        const QString path = url.toLocalFile();
        if (path.isEmpty())
            continue;
        const Packet::PacketType type = Packet::typeForFile(path);
        if (!target->sendFile(path, type))
            appendLog(QStringLiteral("[错误] 发送失败: %1").arg(path));
        else
            appendLog(QStringLiteral("开始发送: %1").arg(QFileInfo(path).fileName()));
    }
    event->acceptProposedAction();
}

// ==================== 事件过滤器：拖拽接收区 ====================

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->dropArea) {
        switch (event->type()) {
        case QEvent::DragEnter: {
            auto *de = static_cast<QDragEnterEvent *>(event);
            if (de->mimeData()->hasUrls() && !m_sessions.isEmpty()) {
                de->acceptProposedAction();
                setDragHighlight(true);
            }
            return true;
        }
        case QEvent::DragMove: {
            auto *dm = static_cast<QDragMoveEvent *>(event);
            if (dm->mimeData()->hasUrls())
                dm->acceptProposedAction();
            return true;
        }
        case QEvent::DragLeave:
            setDragHighlight(false);
            return true;
        case QEvent::Drop: {
            auto *drope = static_cast<QDropEvent *>(event);
            drope->acceptProposedAction();
            setDragHighlight(false);
            dropEvent(drope);   // 复用窗口级 dropEvent 的发送逻辑
            return true;
        }
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// ==================== 传输进度槽 ====================

void MainWindow::onSendProgress(qint64 sent, qint64 total)
{
    ui->sendProgress->setMaximum(int(total));
    ui->sendProgress->setValue(int(sent));
}

void MainWindow::onSendFinished(const QString &fileName, qint64 fileSize)
{
    ui->sendProgress->setValue(ui->sendProgress->maximum());
    appendLog(QStringLiteral("发送完成: %1 (%2 字节)").arg(fileName).arg(fileSize));
}

void MainWindow::onReceiveStarted(const QString &fileName, qint64 fileSize, Packet::PacketType type)
{
    ui->recvProgress->setMaximum(int(fileSize));
    ui->recvProgress->setValue(0);
    appendLog(QStringLiteral("开始接收%1: %2 (%3 字节)")
                  .arg(Packet::typeToString(type), fileName).arg(fileSize));
}

void MainWindow::onReceiveProgress(qint64 received, qint64 total)
{
    ui->recvProgress->setMaximum(int(total));
    ui->recvProgress->setValue(int(received));
}

void MainWindow::onReceiveFinished(const QString &fileName, const QString &savedPath,
                                   Packet::PacketType type)
{
    ui->recvProgress->setValue(ui->recvProgress->maximum());
    appendLog(QStringLiteral("接收完成%1: %2 → 已保存为 %3")
                  .arg(Packet::typeToString(type), fileName, savedPath));
    statusBar()->showMessage(QStringLiteral("文件已保存: %1").arg(savedPath), 5000);
}

void MainWindow::onTransferError(const QString &message)
{
    appendLog(QStringLiteral("[传输错误] %1").arg(message));
    statusBar()->showMessage(QStringLiteral("传输错误: %1").arg(message));
}

// ==================== 私有辅助 ====================

void MainWindow::appendLog(const QString &text)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    ui->logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(stamp, text));
}

void MainWindow::updateButtonStates()
{
    const bool listening = m_server->isListening();
    ui->btnStart->setEnabled(!listening);
    ui->btnStop->setEnabled(listening);
    ui->btnOpenFile->setEnabled(!m_sessions.isEmpty());
    ui->stateLabel->setText(listening
        ? QStringLiteral("监听中: %1").arg(m_listenPort)
        : QStringLiteral("未监听"));
}

void MainWindow::updateClientCount()
{
    ui->clientCountLabel->setText(
        QStringLiteral("客户端数: %1").arg(m_sessions.size()));
}

void MainWindow::setDragHighlight(bool on)
{
    ui->dropArea->setProperty("dragOver", on);
    style()->unpolish(ui->dropArea);
    style()->polish(ui->dropArea);
}
