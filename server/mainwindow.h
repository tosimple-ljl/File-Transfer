#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QString>
#include <QList>

#include "packet.h"

class QTcpServer;
class SettingsDialog;
class TransferManager;

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

/**
 * @brief 服务端主窗口（View 层）
 *
 * 职责：
 *   - 通过 QTcpServer 监听端口，接受多个客户端连接；
 *   - 每个连接对应一个 TransferManager 会话（Model 层）；
 *   - 拖入文件 = 向最近接入的客户端发送（与客户端对称）。
 *
 * 事件处理体现：虚函数重写（dragEnterEvent 等）+ 事件过滤器（dropArea）。
 */
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    // ---- 虚函数重写：窗口级拖拽 ----
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    /** @brief 事件过滤器：拖拽接收区的高亮与落下处理 */
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onStartClicked();        ///< 开始监听
    void onStopClicked();         ///< 停止监听
    void onSettingsClicked();     ///< 打开服务设置对话框
    void onOpenFileClicked();     ///< 选择文件向客户端发送
    void onNewConnection();       ///< 处理 newConnection：建立会话

    // 设置对话框跨窗口通信接收槽
    void onSettingsConfirmed(const QString &ip, quint16 port, const QString &saveDir);

    // 传输进度/完成/错误（由各会话转发）
    void onSendProgress(qint64 sent, qint64 total);
    void onSendFinished(const QString &fileName, qint64 fileSize);
    void onReceiveStarted(const QString &fileName, qint64 fileSize, Packet::PacketType type);
    void onReceiveProgress(qint64 received, qint64 total);
    void onReceiveFinished(const QString &fileName, const QString &savedPath, Packet::PacketType type);
    void onTransferError(const QString &message);

private:
    void setupUiExtras();
    void appendLog(const QString &text);
    void updateButtonStates();
    void updateClientCount();
    void setDragHighlight(bool on);

    Ui::MainWindow      *ui;
    QTcpServer          *m_server      = nullptr;  ///< 监听 socket
    QList<TransferManager*> m_sessions;            ///< 每个客户端一个会话
    SettingsDialog      *m_settingsDlg = nullptr;
    quint16             m_listenPort   = 0;        ///< 监听端口
    QString             m_saveDir;                 ///< 接收文件保存目录
};

#endif // MAINWINDOW_H
