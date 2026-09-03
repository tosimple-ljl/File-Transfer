#------------------------------------------------------------------
# common.pri —— 客户端/服务端共用的源码（协议、收发器、设置对话框）
# 由 client.pro / server.pro 通过 include() 引入
#------------------------------------------------------------------

INCLUDEPATH += $$PWD

HEADERS += \
    $$PWD/packet.h \
    $$PWD/file_sender.h \
    $$PWD/file_receiver.h \
    $$PWD/transfer_manager.h \
    $$PWD/settingsdialog.h

SOURCES += \
    $$PWD/packet.cpp \
    $$PWD/file_sender.cpp \
    $$PWD/file_receiver.cpp \
    $$PWD/transfer_manager.cpp \
    $$PWD/settingsdialog.cpp

FORMS += \
    $$PWD/settingsdialog.ui
