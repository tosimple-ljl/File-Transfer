#-------------------------------------------------
# 客户端子项目
#-------------------------------------------------
QT       += core gui network

greaterThan(QT_MAJOR_VERSION, 4): QT += widgets

TEMPLATE = app
CONFIG   += c++17

TARGET   = FileTransferClient

# 引入公共源码（协议、收发器、设置对话框）
include(../common/common.pri)

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h

FORMS += \
    mainwindow.ui

# 资源（图标/QSS）统一放 common 下，qrc 以 common 为工作目录
RESOURCES += \
    ../common/resources.qrc

# 输出目录统一到工程根目录的 bin/ 下，方便两端联调
DESTDIR = ../bin
