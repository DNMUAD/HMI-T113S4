#ifndef HMI_MODELS_WIFISCANMODEL_H
#define HMI_MODELS_WIFISCANMODEL_H

#include <QAbstractListModel>
#include <QList>
#include <QString>

struct WifiAccessPoint
{
    QString ssid;
    QString bssid;
    int     signalDbm;      // -127 表示未知
    bool    secure;

    WifiAccessPoint()
        : signalDbm(-127)
        , secure(false)
    {
    }
};

// 设计模式 #13：模型-视图（MVC / Model-View）+ 适配器（Adapter）
//
// 「wpa_cli scan_results」或「iw dev wlan0 scan」的原始文本是设备相关、格式不稳定的，
// 直接塞进 QPlainTextEdit 只能看不能用。这里用 QAbstractListModel 做适配：
//   · Model 负责解析原始输出并按信号强度排序去重；
//   · View（QListView）只负责显示与选中；
//   · 页面拿到 index 后取 SSID 去连接，不关心文本格式。
// 换命令、换芯片解析规则时只改这个文件，页面不动。
class WifiScanModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Role
    {
        SsidRole = Qt::UserRole + 1,
        BssidRole,
        SignalRole,
        SecureRole,
        DetailRole
    };

    explicit WifiScanModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;

    void setRawOutput(const QString &raw);      // 适配：原始输出 → 行数据
    void clear();

    WifiAccessPoint accessPoint(int row) const;
    int count() const;

    // 把 iw / wpa_cli 输出里的 \xNN 转义还原成真实字节（中文 SSID 必需）。
    // 页面显示「原始输出」时也调它，否则文本框里是一串 \xe8\x8f\xa0。
    // setRawOutput() 内部已自动调用，重复调用无副作用（无 \x 即原样返回）。
    static QString decodeEscaped(const QString &raw);

private:
    QList<WifiAccessPoint> m_points;
};

#endif // HMI_MODELS_WIFISCANMODEL_H
