#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include <QFileDialog>
#include <QHostAddress>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);

    // 信号与槽连接（带中文注释）：
    // 1) 对话框按钮盒：确定/取消
    connect(ui->buttonBox, &QDialogButtonBox::accepted,
            this, &SettingsDialog::onOkClicked);
    connect(ui->buttonBox, &QDialogButtonBox::rejected,
            this, &SettingsDialog::reject);

    // 2) 浏览按钮：弹出目录选择框
    connect(ui->btnBrowse, &QPushButton::clicked,
            this, &SettingsDialog::onBrowseClicked);

    // 默认保存到用户“下载”目录
    if (ui->savePathEdit->text().isEmpty()) {
        ui->savePathEdit->setText(
            QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    }
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

QString SettingsDialog::ip() const        { return ui->ipEdit->text().trimmed(); }
quint16 SettingsDialog::port() const      { return static_cast<quint16>(ui->portEdit->value()); }
QString SettingsDialog::saveDir() const   { return ui->savePathEdit->text().trimmed(); }

void SettingsDialog::setIp(const QString &ip)      { ui->ipEdit->setText(ip); }
void SettingsDialog::setPort(quint16 port)         { ui->portEdit->setValue(port); }
void SettingsDialog::setSaveDir(const QString &d)  { ui->savePathEdit->setText(d); }

void SettingsDialog::hideIpRow()
{
    ui->ipLabel->hide();
    ui->ipEdit->hide();
}

void SettingsDialog::onOkClicked()
{
    // ---- 简单校验，保证传给主窗口的参数合法 ----
    const QRegularExpression ipRe(
        QStringLiteral(R"(^((\d{1,2}|1\d\d|2[0-4]\d|25[0-5])\.){3}(\d{1,2}|1\d\d|2[0-4]\d|25[0-5])$)"));
    if (ui->ipEdit->isVisible() && !ipRe.match(ip()).hasMatch()) {
        QMessageBox::warning(this, QStringLiteral("参数错误"),
                             QStringLiteral("IP 地址格式不正确"));
        return;
    }
    if (saveDir().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("参数错误"),
                             QStringLiteral("请设置接收文件的保存目录"));
        return;
    }

    // ---- 跨窗口通信核心：用自定义信号把参数传给主窗口 ----
    emit settingsConfirmed(ip(), port(), saveDir());
    accept();
}

void SettingsDialog::onBrowseClicked()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择接收文件的保存目录"), saveDir());
    if (!dir.isEmpty())
        ui->savePathEdit->setText(dir);
}
