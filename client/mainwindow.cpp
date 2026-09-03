#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "settingsdialog.h"
#include "transfer_manager.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QMimeData>
#include <QStatusBar>
#include <QStyle>
#include <QTcpSocket>
#include <QUrl>
#include <QDateTime>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    // 创建 socket 与会话管理器（Model 层），UI 不直接处理网络细节
    m_socket   = new QTcpSocket(this);
    m_transfer = new TransferManager(m_socket, this);

    setupUiExtras();

    // ================ 信号与槽连接（带中文注释） ================
    // 1) 按钮点击 → 对应处理槽
    connect(ui->btnConnect,    &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui->btnDisconnect, &QPushButton::clicked, this, &MainWindow::onDisconnectClicked);
    connect(ui->btnSettings,   &QPushButton::clicked, this, &MainWindow::onSettingsClicked);
    connect(ui->btnOpenFile,   &QPushButton::clicked, this, &MainWindow::onOpenFileClicked);

    // 2) 菜单动作
    connect(ui->actionSettings, &QAction::triggered, this, &MainWindow::onSettingsClicked);
    connect(ui->actionQuit,     &QAction::triggered, this, &MainWindow::close);
    connect(ui->actionAbout,    &QAction::triggered, this, [this] {
        QMessageBox::information(this, QStringLiteral("关于"),
            QStringLiteral("基于 Qt TCP Socket 的文件传输工具\n"
                           "课程作业 - 客户端\n支持文本/图片文件可靠传输"));
    });

    // 3) socket 状态变化 → 界面反馈
    connect(m_socket, &QTcpSocket::connected,    this, &MainWindow::onConnected);
    connect(m_socket, &QTcpSocket::disconnected, this, [this] {
        onDisconnected(m_socket->peerAddress().toString() +
                       QStringLiteral(":") + QString::number(m_socket->peerPort()));
    });
    // 网络错误（连接被拒、超时等）→ 状态栏 + 弹窗提示，保证程序不崩溃
    connect(m_socket, &QTcpSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) {
        onConnectionError(m_socket->errorString());
    });

    // 4) TransferManager 转发传输进度/完成/错误 → 界面刷新
    connect(m_transfer, &TransferManager::sendProgress,      this, &MainWindow::onSendProgress);
    connect(m_transfer, &TransferManager::sendFinished,      this, &MainWindow::onSendFinished);
    connect(m_transfer, &TransferManager::sendError,         this, &MainWindow::onTransferError);
    connect(m_transfer, &TransferManager::receiveStarted,    this, &MainWindow::onReceiveStarted);
    connect(m_transfer, &TransferManager::receiveProgress,   this, &MainWindow::onReceiveProgress);
    connect(m_transfer, &TransferManager::receiveFinished,   this, &MainWindow::onReceiveFinished);
    connect(m_transfer, &TransferManager::receiveError,      this, &MainWindow::onTransferError);
    connect(m_transfer, &TransferManager::peerDisconnected,  this, &MainWindow::onDisconnected);
    connect(m_transfer, &TransferManager::connectionError,   this, &MainWindow::onConnectionError);

    updateButtonStates();
    statusBar()->showMessage(QStringLiteral("就绪。请先在“连接设置”中配置服务器地址。"));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::setupUiExtras()
{
    // ---- 拖拽接收区：允许接受拖拽 + 安装事件过滤器 ----
    // 拖拽事件先落在 dropArea 上，由 eventFilter() 拦截处理实现高亮；
    // 落在窗口其他区域时走虚函数 dragEnterEvent/dropEvent。
    ui->dropArea->setAcceptDrops(true);
    ui->dropArea->installEventFilter(this);
    setAcceptDrops(true);

    // 日志区随内容自动滚动到底部
    ui->logEdit->setMaximumBlockCount(500);

    updateButtonStates();
}

// ==================== 虚函数重写：窗口级拖拽 ====================

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    // 只要拖进来的是“本地文件 URL 列表”就接受
    if (event->mimeData()->hasUrls()) {
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
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        const QString path = url.toLocalFile();
        if (!path.isEmpty())
            sendLocalFile(path);
    }
    event->acceptProposedAction();
}

// ==================== 事件过滤器：拖拽接收区 ====================

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->dropArea) {
        switch (event->type()) {
        case QEvent::DragEnter: {
            // 文件进入拖拽接收区：高亮并接受
            auto *de = static_cast<QDragEnterEvent *>(event);
            if (de->mimeData()->hasUrls()) {
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
            // 松开鼠标：取出拖入的本地文件并逐个发送
            auto *drope = static_cast<QDropEvent *>(event);
            setDragHighlight(false);
            const QList<QUrl> urls = drope->mimeData()->urls();
            for (const QUrl &url : urls) {
                const QString path = url.toLocalFile();
                if (!path.isEmpty())
                    sendLocalFile(path);
            }
            drope->acceptProposedAction();
            return true;
        }
        default:
            break;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// ==================== 按钮 / 菜单槽 ====================

void MainWindow::onConnectClicked()
{
    if (m_socket->state() != QAbstractSocket::UnconnectedState) {
        appendLog(QStringLiteral("已在连接状态，忽略重复连接请求"));
        return;
    }

    // 参数为空时给默认值，保证开箱可用
    if (m_serverIp.isEmpty())
        m_serverIp = QStringLiteral("127.0.0.1");
    if (m_serverPort == 0)
        m_serverPort = 8888;

    appendLog(QStringLiteral("正在连接 %1:%2 ...").arg(m_serverIp).arg(m_serverPort));
    // 连接指定 IP 和端口（异步，结果由 connected / errorOccurred 信号通知）
    m_socket->connectToHost(m_serverIp, m_serverPort);
    statusBar()->showMessage(QStringLiteral("连接中…"));
}

void MainWindow::onDisconnectClicked()
{
    m_socket->disconnectFromHost();
    appendLog(QStringLiteral("主动断开连接"));
}

void MainWindow::onSettingsClicked()
{
    // 惰性创建设置对话框（窗口通信：见 onSettingsConfirmed）
    if (!m_settingsDlg) {
        m_settingsDlg = new SettingsDialog(this);
        // 跨窗口通信核心：对话框 emit settingsConfirmed → 主窗口槽接收
        connect(m_settingsDlg, &SettingsDialog::settingsConfirmed,
                this, &MainWindow::onSettingsConfirmed);
    }
    m_settingsDlg->setIp(m_serverIp);
    m_settingsDlg->setPort(m_serverPort);
    m_settingsDlg->show();
    m_settingsDlg->raise();
    m_settingsDlg->activateWindow();
}

void MainWindow::onOpenFileClicked()
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("请先连接服务器再发送文件"));
        return;
    }
    const QStringList files = QFileDialog::getOpenFileNames(
        this, QStringLiteral("选择要发送的文件"), QString(),
        QStringLiteral("所有文件 (*);;文本文件 (*.txt);;图片文件 (*.png *.jpg *.jpeg *.bmp)"));
    for (const QString &f : files)
        sendLocalFile(f);
}

// ==================== 设置对话框回调（窗口间通信） ====================

void MainWindow::onSettingsConfirmed(const QString &ip, quint16 port, const QString &saveDir)
{
    // 接收设置对话框传来的参数（通过信号与槽实现的跨窗口通信）
    m_serverIp   = ip;
    m_serverPort = port;
    m_transfer->setSaveDir(saveDir);

    appendLog(QStringLiteral("设置已更新: IP=%1 端口=%2 保存目录=%3")
                  .arg(ip).arg(port).arg(saveDir));
    statusBar()->showMessage(QStringLiteral("设置已更新"), 3000);
}

// ==================== socket 状态槽 ====================

void MainWindow::onConnected()
{
    appendLog(QStringLiteral("已连接到服务器 %1:%2")
                  .arg(m_socket->peerAddress().toString())
                  .arg(m_socket->peerPort()));
    statusBar()->showMessage(QStringLiteral("已连接"), 5000);
    updateButtonStates();
}

void MainWindow::onDisconnected(const QString &peer)
{
    appendLog(QStringLiteral("连接已断开 (%1)").arg(peer));
    statusBar()->showMessage(QStringLiteral("未连接"));
    updateButtonStates();
}

void MainWindow::onConnectionError(const QString &message)
{
    // 错误处理：状态栏 + 弹窗，保证程序不崩溃
    appendLog(QStringLiteral("[错误] %1").arg(message));
    statusBar()->showMessage(QStringLiteral("网络错误: %1").arg(message));
    if (m_socket->state() == QAbstractSocket::UnconnectedState)
        QMessageBox::warning(this, QStringLiteral("网络错误"), message);
    updateButtonStates();
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

void MainWindow::sendLocalFile(const QString &path)
{
    if (m_socket->state() != QAbstractSocket::ConnectedState) {
        QMessageBox::warning(this, QStringLiteral("提示"),
                             QStringLiteral("未连接服务器，无法发送文件"));
        return;
    }
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) {
        appendLog(QStringLiteral("[错误] 文件不可读: %1").arg(path));
        return;
    }

    const Packet::PacketType type = Packet::typeForFile(path);
    appendLog(QStringLiteral("开始发送%1: %2 (%3 字节)")
                  .arg(Packet::typeToString(type), info.fileName())
                  .arg(info.size()));

    if (!m_transfer->sendFile(path, type)) {
        appendLog(QStringLiteral("[错误] 文件发送失败: %1").arg(info.fileName()));
    }
}

void MainWindow::appendLog(const QString &text)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    ui->logEdit->appendPlainText(QStringLiteral("[%1] %2").arg(stamp, text));
}

void MainWindow::updateButtonStates()
{
    const bool connected = m_socket->state() == QAbstractSocket::ConnectedState;
    ui->btnConnect->setEnabled(!connected);
    ui->btnDisconnect->setEnabled(connected);
    ui->btnOpenFile->setEnabled(connected);
    ui->stateLabel->setText(connected
        ? QStringLiteral("已连接 %1:%2").arg(m_socket->peerAddress().toString())
                                          .arg(m_socket->peerPort())
        : QStringLiteral("未连接"));
}

void MainWindow::setDragHighlight(bool on)
{
    // 通过动态属性切换 QSS 样式（拖拽高亮），切换后需重新 polish 生效
    ui->dropArea->setProperty("dragOver", on);
    style()->unpolish(ui->dropArea);
    style()->polish(ui->dropArea);
}
