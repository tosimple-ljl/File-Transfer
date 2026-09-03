#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>

#include "packet.h"

class QTcpSocket;
class TransferManager;
class SettingsDialog;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

/**
 * @brief 客户端主窗口（View 层）
 *
 * 职责：界面展示与用户交互；网络收发全部委托 TransferManager（Model 层）。
 *
 * 事件处理要求体现（虚函数重写 + 事件过滤器）：
 *   - dragEnterEvent / dropEvent 等虚函数在“整个窗口”上生效；
 *   - 拖拽接收区 dropArea 上通过 installEventFilter 安装事件过滤器，
 *     在 eventFilter() 中处理 DragEnter/DragLeave/Drop，实现精确的高亮反馈。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    // ---- 虚函数重写：窗口级拖拽（拖到窗口任意位置都能发送） ----
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    /** @brief 事件过滤器：处理拖拽接收区上的拖拽事件（高亮 + 落下发送） */
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    // ---- 按钮 / 菜单 ----
    void onConnectClicked();      ///< 连接服务器
    void onDisconnectClicked();   ///< 断开连接
    void onSettingsClicked();     ///< 打开设置对话框
    void onOpenFileClicked();     ///< 选择文件发送

    // ---- 设置对话框跨窗口通信的接收槽 ----
    void onSettingsConfirmed(const QString &ip, quint16 port, const QString &saveDir);

    // ---- socket 状态（由 TransferManager 转发或本类连接） ----
    void onConnected();
    void onDisconnected(const QString &peer);
    void onConnectionError(const QString &message);

    // ---- 传输进度（驱动进度条与日志） ----
    void onSendProgress(qint64 sent, qint64 total);
    void onSendFinished(const QString &fileName, qint64 fileSize);
    void onReceiveStarted(const QString &fileName, qint64 fileSize, Packet::PacketType type);
    void onReceiveProgress(qint64 received, qint64 total);
    void onReceiveFinished(const QString &fileName, const QString &savedPath, Packet::PacketType type);
    void onTransferError(const QString &message);

private:
    void setupUiExtras();         ///< ui 控件的补充设置（图标、事件过滤器等）
    void appendLog(const QString &text);        ///< 追加带时间戳的日志
    void sendLocalFile(const QString &path);    ///< 发送一个本地文件
    void updateButtonStates();                  ///< 根据连接状态刷新按钮可用性
    void setDragHighlight(bool on);             ///< 拖拽高亮开关（QSS 属性切换）

    Ui::MainWindow   *ui;
    QTcpSocket       *m_socket      = nullptr;  ///< 与服务端的连接
    TransferManager  *m_transfer    = nullptr;  ///< 会话管理（收发委托对象）
    SettingsDialog   *m_settingsDlg = nullptr;  ///< 设置对话框（窗口通信）
    QString           m_serverIp;               ///< 服务器 IP
    quint16           m_serverPort = 0;         ///< 服务器端口
};

#endif // MAINWINDOW_H
