INCLUDEPATH += $$PWD

HEADERS += \
    $$PWD/processrunner.h \
    $$PWD/wifiservice.h \
    $$PWD/btservice.h \
    $$PWD/btworker.h \
    $$PWD/ibtbackend.h \
    $$PWD/fakebtbackend.h \
    $$PWD/ethservice.h \
    $$PWD/adbservice.h \
    $$PWD/i2cservice.h \
    $$PWD/rs485service.h \
    $$PWD/gpadcservice.h \
    $$PWD/mediaservice.h \
    $$PWD/fileservice.h \
    $$PWD/webshareservice.h \
    $$PWD/upgradeservice.h

SOURCES += \
    $$PWD/processrunner.cpp \
    $$PWD/wifiservice.cpp \
    $$PWD/btservice.cpp \
    $$PWD/btworker.cpp \
    $$PWD/btbackendfactory.cpp \
    $$PWD/fakebtbackend.cpp \
    $$PWD/ethservice.cpp \
    $$PWD/adbservice.cpp \
    $$PWD/i2cservice.cpp \
    $$PWD/rs485service.cpp \
    $$PWD/gpadcservice.cpp \
    $$PWD/mediaservice.cpp \
    $$PWD/fileservice.cpp \
    $$PWD/webshareservice.cpp \
    $$PWD/upgradeservice.cpp

# 板端专用：只交叉编译时编入。x86 桌面 Kit 的 sysroot 里没有
# <bt_manager.h> 和 libbtmg.so，无条件编会直接编译不过。
# 注意 qmake 里 DEFINES 只是编译宏，不会自动生成同名 scope 变量，
# 所以这里必须用 contains(DEFINES, ...) 判断。
contains(DEFINES, HMI_HAVE_BT) {
    HEADERS += $$PWD/btmgbackend.h
    SOURCES += $$PWD/btmgbackend.cpp
}
