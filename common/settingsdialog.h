#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QDialog>

/**
 * @brief 传输设置对话框（跨窗口通信必做项）
 *
 * 通过 Qt Designer 的 .ui 文件设计界面，
 * 点击“确定”后以【自定义信号】把 IP / 端口 / 保存目录 传给主窗口，
 * 主窗口通过 信号与槽 接收 —— 体现窗口间通信能力。
 */
namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

    // ---- 参数读写（主窗口也可直接取值/回显） ----
    QString ip() const;
    quint16 port() const;
    QString saveDir() const;

    void setIp(const QString &ip);
    void setPort(quint16 port);
    void setSaveDir(const QString &dir);

    /** @brief 服务端不显示 IP 一栏（只设监听端口与保存目录） */
    void hideIpRow();

signals:
    /**
     * @brief 点击“确定”并校验通过后发出
     * @param ip      服务器 IP（客户端使用）
     * @param port    端口
     * @param saveDir 接收文件保存目录
     */
    void settingsConfirmed(const QString &ip, quint16 port, const QString &saveDir);

private slots:
    void onOkClicked();          ///< 确定按钮：校验并发出 settingsConfirmed
    void onBrowseClicked();      ///< 浏览按钮：选择保存目录

private:
    Ui::SettingsDialog *ui;
};

#endif // SETTINGSDIALOG_H
