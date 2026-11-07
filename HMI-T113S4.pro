# HMI-T113S4 — 全志 T113-S4 板载 HMI 主程序
# C++11 + qmake。二进制名保留 HMI-T113S4；部署/升级到板端时才改名为 /usr/bin/QtApplication。

QT += core gui widgets multimedia network serialport

# M5（CAN）打开方式：qmake CONFIG+=hmi_can
# 需要 SDK 提供 qt5serialbus 与 libqtsocketcanbus.so；M1 不依赖它，桌面 Kit 可直接编。
hmi_can {
    QT += serialbus
    DEFINES += HMI_HAVE_CAN
}

TEMPLATE = app
TARGET   = HMI-T113S4

CONFIG += c++11
CONFIG -= app_bundle
QMAKE_CXXFLAGS += -Wno-unused-parameter -Wno-reorder

INCLUDEPATH += $$PWD/pages $$PWD/services $$PWD/widgets $$PWD/core $$PWD/models

# core/：设计模式基础设施（单例、命令策略、命令对象、状态总线、服务与页面注册表）
# models/：QAbstractItemModel 视图模型
include(core/core.pri)
include(models/models.pri)
include(pages/pages.pri)
include(services/services.pri)
include(widgets/widgets.pri)

SOURCES += \
    main.cpp \
    mainwindow.cpp

HEADERS += \
    mainwindow.h

RESOURCES += resources/resources.qrc

# 翻译：桌面 Kit 由 qmake 内嵌到 :/i18n/；
# 无 lrelease 的环境传 CONFIG+=hmi_no_lrelease，翻译改由 tools/gen-i18n.sh 生成。
TRANSLATIONS += resources/i18n/HMI-T113S4_zh_CN.ts
!hmi_no_lrelease {
    CONFIG += lrelease embed_translations
}
