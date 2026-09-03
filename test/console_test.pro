#-------------------------------------------------
# 控制台自测程序：验证协议组包/收发的正确性（无 GUI）
# 不加入顶层 SUBDIRS，手动构建：qmake6 && make && ./bin/console_test
#-------------------------------------------------
QT       += core network
QT       -= gui

TEMPLATE = app
CONFIG   += c++17 console

TARGET   = console_test
DESTDIR  = ../bin

# 只引入协议与收发器（控制台版不需要 Widgets 的设置对话框）
INCLUDEPATH += ../common

HEADERS += \
    ../common/packet.h \
    ../common/file_sender.h \
    ../common/file_receiver.h

SOURCES += \
    ../common/packet.cpp \
    ../common/file_sender.cpp \
    ../common/file_receiver.cpp \
    console_test.cpp
