#ifndef HMI_CORE_BTDEVICE_H
#define HMI_CORE_BTDEVICE_H

#include <QMetaType>
#include <QString>

// 蓝牙设备值对象（观察者链上流动的「事件对象」，对应 devicestatus.h 的角色）。
//
// 为什么单独抽一个 core/ 下的值对象：
//   models/BtScanModel 需要它做行数据，services/IBtBackend 需要它做回调参数。
//   若放在 services/ 里，models 就要反向依赖 services，破坏「依赖永远向下」的约定。
//   放到 core/ 后两边都只依赖 core，和 WifiAccessPoint 放在 wifiscanmodel.h 里
//   是同一个考虑，只是这里被两层共用，所以往上提了一级。
//
// 字段可信度来自 SDK 源码核对（btmanager/src/common/common.c:231 _get_device_properties）：
//   可靠：address / paired / connected / deviceClass
//   name：发现阶段常常为空，而且 btmanager 在 remote_name == NULL 时
//         直接 return、根本不发发现回调（bt_bluez_signals.c:63）——所以能到这里的
//         设备一般都带名字，但已配对列表里可能有空名
//   rssi：发现回调里基本是 0（InterfacesAdded 通常不带 RSSI），
//         实时值只能靠 RSSI 更新回调单独维护。0 必须当成「未知」，不能拿去排序
//   icon：BlueZ 给的分类图标名（audio-card / phone / computer），不是图片路径
struct BtDeviceRecord
{
    QString address;        // "AA:BB:CC:DD:EE:FF"
    QString name;
    QString icon;
    int     rssi;           // 0 = 未知
    bool    paired;
    bool    connected;
    quint32 deviceClass;    // BR/EDR Class；0 表示未知（BLE 设备与部分手机都是 0）

    BtDeviceRecord()
        : rssi(0)
        , paired(false)
        , connected(false)
        , deviceClass(0)
    {
    }

    bool isValid() const
    {
        return !address.isEmpty();
    }

    // 地址是唯一键，大小写不敏感（BlueZ 输出统一大写，但配置里可能被手写成小写）
    bool sameDevice(const BtDeviceRecord &other) const
    {
        return address.compare(other.address, Qt::CaseInsensitive) == 0;
    }
};

// ── 事件枚举 ────────────────────────────────────────────────────
// 放在 core/ 而不是 services/ibtbackend.h：界面（pages/）与模型（models/）
// 都要用这些值，而它们不该反向依赖后端接口所在的 services/。
// 取值与 btmanager 的 btmg_scan_state_t / btmg_bond_state_t / btmg_adapter_state_t
// 一一对应，换后端时对齐这里即可。
enum BtAdapterState
{
    BtAdapterOff = 0,
    BtAdapterTurningOn,
    BtAdapterOn,
    BtAdapterTurningOff
};

enum BtScanState
{
    BtScanStarted = 0,
    BtScanStartFailed,
    BtScanStopped
};

enum BtBondState
{
    BtBondUnbonded = 0,
    BtBondBonding,
    BtBondBonded,
    BtBondBondFailed
};

enum BtScanType
{
    BtScanAuto = 0,
    BtScanClassic,
    BtScanLe
};

Q_DECLARE_METATYPE(BtDeviceRecord)

#endif // HMI_CORE_BTDEVICE_H
