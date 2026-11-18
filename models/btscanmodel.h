#ifndef HMI_MODELS_BTSCANMODEL_H
#define HMI_MODELS_BTSCANMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QString>

#include "btdevice.h"

// 设计模式 #13：模型-视图（MVC）+ 适配器（Adapter）
//
// 与 models/wifiscanmodel.* 同构：设备层（IBtBackend）只吐结构化记录，
// 页面拿 QListView 显示、双击取地址去配对，不关心数据从 libbtmg 还是假后端来。
//
// 与 WiFi 模型的两点差别，都是蓝牙的实际情况决定的：
//
// 1) 列表按「地址」去重，不是按名字。蓝牙设备没有 SSID 那种天然分组键，
//    同一设备会重复上报（InterfacesAdded + PropertiesChanged），
//    而名字在发现阶段经常为空——按名字去重会把不同设备合成一条。
//
// 2) rssi == 0 一律当「未知」显示成 --。真实现里发现回调的 RSSI 基本是 0
//    （InterfacesAdded 通常不带该属性），实时值只能靠 RSSI 更新回调补。
//    把 0 显示成 0 dBm 会让人以为信号极差。
//
// 不排序：蓝牙设备的 rssi 大多未知，按它排序等于随机；按发现顺序排列
// 更符合「刚扫到的在最下面」的直觉，也和 BlueZ 的推送顺序一致。
class BtScanModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role
    {
        AddressRole = Qt::UserRole + 1,
        NameRole,
        RssiRole,
        PairedRole,
        ConnectedRole,
        DetailRole
    };

    explicit BtScanModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void clear();
    void addOrUpdate(const BtDeviceRecord &device);
    void updateRssi(const QString &address, int rssi);
    void removeDevice(const QString &address);
    void setPaired(const QString &address, bool paired);

    BtDeviceRecord deviceAt(int row) const;
    int count() const;
    bool contains(const QString &address) const;

private:
    int indexOf(const QString &address) const;

    QList<BtDeviceRecord> m_devices;
};

#endif // HMI_MODELS_BTSCANMODEL_H
