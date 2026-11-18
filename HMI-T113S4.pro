# HMI-T113S4 — 全志 T113-S4 板载 HMI 主程序
# C++11 + qmake。二进制名保留 HMI-T113S4；部署/升级到板端时才改名为 QtApplication。

QT += core gui widgets multimedia network serialport

# M5（CAN）打开方式：qmake CONFIG+=hmi_can
# 需要 SDK 提供 qt5serialbus 与 libqtsocketcanbus.so；M1 不依赖它，桌面 Kit 可直接编。
hmi_can {
    QT += serialbus
    DEFINES += HMI_HAVE_CAN
}

TEMPLATE = app
#TARGET   = HMI-T113S4

CONFIG += c++11
CONFIG -= app_bundle
QMAKE_CXXFLAGS += -Wno-unused-parameter -Wno-reorder

# 两个构建目标，只区分 Linux 桌面与 Linux 板端，不涉及 Windows：
#   HMI_DESKTOP         x86/x86_64 桌面 Kit：窗口化、读程序目录配置、用户媒体库
#   HMI_DEVICE_TARGET   ARM 交叉编译：全屏、/etc/hmi.ini、板端挂载点与设备节点
# contains() 是子串匹配，arm 已能命中 armv7/aarch64 等取值
contains(QT_ARCH, arm) {
    TARGET   = QtApplication
    DEFINES += HMI_DEVICE_TARGET

    # 蓝牙：板端用全志 btmanager（libbtmg.so）。
    #
    # 头文件和库都在交叉 sysroot 的 usr/include、usr/lib 下，
    # qmake -query QT_SYSROOT 指向的正是这个 sysroot，不需要额外 INCLUDEPATH。
    #
    # 链接不能只写 -lbtmg。libbtmg.so 是**不完整的**：它把 bluez 的部分内部
    # 实现和 GATT 协议栈留给了外部（实测 `undefined reference to
    # gatt_db_remove_service / bt_gatt_client_new / mainloop_quit / log_print ...`）。
    # 完整依赖列表取自 SDK 里 btmanager demo 的 Makefile
    # （platform/allwinner/wireless/btmanager/demo/Makefile:3-17），顺序也照抄——
    # -lbtmg 必须在依赖它的库之前，静态库 -lbluetooth-internal / -lshared-mainloop
    # 必须放在最后，否则链接器不会回头再解析一次。
    DEFINES += HMI_HAVE_BT
    LIBS    += -lbluetooth -ljson-c -lbtmg -ldl -lm -lresolv -lwirelesscom
    LIBS    += -lglib-2.0 -lgio-2.0 -lgobject-2.0 -lasound -ldbus-1
    LIBS    += -lbluetooth-internal -lshared-mainloop
} else {
    TARGET   = HMI-T113S4
    DEFINES += HMI_DESKTOP
    # 桌面 Kit 不链接 libbtmg（sysroot 里没有）：
    # 蓝牙走 services/fakebtbackend.*，由 services/ibtbackend.h 的工厂选择。
}

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
# 翻译改由 tools/gen-i18n.sh 生成。
TRANSLATIONS += resources/i18n/HMI-T113S4_zh_CN.ts
