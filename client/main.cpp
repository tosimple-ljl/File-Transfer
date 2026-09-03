#include "mainwindow.h"

#include <QApplication>
#include <QFile>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // ---- 加载 QSS 样式表（从 Qt 资源系统读取，美化全局界面） ----
    QFile qss(QStringLiteral(":/style.qss"));
    if (qss.open(QFile::ReadOnly)) {
        qApp->setStyleSheet(QString::fromUtf8(qss.readAll()));
        qss.close();
    }

    MainWindow w;
    w.show();
    return a.exec();
}
