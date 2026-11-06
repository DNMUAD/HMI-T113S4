#include "wifiscanmodel.h"

#include <QRegularExpression>

#include <algorithm>

namespace {

// iw 风格：BSS 3c:cd:5d:.. / SSID: xxx / signal: -42 dBm / RSN: 表示加密
const char *kBssPattern    = "^BSS\\s+([0-9a-fA-F:]{17})";
const char *kSsidPattern   = "^\\s*SSID:\\s*(.+?)\\s*$";
const char *kSignalPattern = "^\\s*signal:\\s*(-?\\d+)";
// 表格式：wifi -s 之类的输出 "1. MyWifi   -45  WPA2"
const char *kTablePattern  = "^\\s*\\d+[.)]\\s+(.+?)\\s{2,}(-?\\d{2,3})\\b(.*)$";

void appendUnique(QList<WifiAccessPoint> *points, const WifiAccessPoint &ap)
{
    for (int i = 0; i < points->size(); ++i) {
        if (points->at(i).ssid != ap.ssid)
            continue;
        // 同名 SSID（2.4G / 5G 双频）只保留信号更强的那条
        if (ap.signalDbm > points->at(i).signalDbm)
            (*points)[i] = ap;
        return;
    }
    points->append(ap);
}

bool strongerFirst(const WifiAccessPoint &a, const WifiAccessPoint &b)
{
    return a.signalDbm > b.signalDbm;
}

} // namespace

WifiScanModel::WifiScanModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int WifiScanModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_points.size();
}

int WifiScanModel::count() const
{
    return m_points.size();
}

WifiAccessPoint WifiScanModel::accessPoint(int row) const
{
    if (row < 0 || row >= m_points.size())
        return WifiAccessPoint();
    return m_points.at(row);
}

QVariant WifiScanModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_points.size())
        return QVariant();

    const WifiAccessPoint &ap = m_points.at(index.row());
    switch (role) {
    case Qt::DisplayRole: {
        const QString signal = ap.signalDbm == -127
                               ? QStringLiteral("--")
                               : QString::number(ap.signalDbm) + QStringLiteral(" dBm");
        return QStringLiteral("%1  %2    %3")
                .arg(ap.secure ? QStringLiteral("[加密]") : QStringLiteral("[开放]"))
                .arg(ap.ssid, signal);
    }
    case SsidRole:   return ap.ssid;
    case BssidRole:  return ap.bssid;
    case SignalRole: return ap.signalDbm;
    case SecureRole: return ap.secure;
    case DetailRole:
        return QStringLiteral("%1 · %2 · %3")
                .arg(ap.secure ? QStringLiteral("加密") : QStringLiteral("开放"),
                     ap.signalDbm == -127 ? QStringLiteral("信号未知")
                                          : QString::number(ap.signalDbm) + QStringLiteral(" dBm"),
                     ap.bssid.isEmpty() ? QStringLiteral("BSSID 未知") : ap.bssid);
    default:
        return QVariant();
    }
}

void WifiScanModel::clear()
{
    beginResetModel();
    m_points.clear();
    endResetModel();
}

void WifiScanModel::setRawOutput(const QString &raw)
{
    QList<WifiAccessPoint> parsed;

    const QRegularExpression bssRx(QString::fromLatin1(kBssPattern));
    const QRegularExpression ssidRx(QString::fromLatin1(kSsidPattern));
    const QRegularExpression signalRx(QString::fromLatin1(kSignalPattern));
    const QRegularExpression tableRx(QString::fromLatin1(kTablePattern));

    const QStringList lines = raw.split(QLatin1Char('\n'));
    WifiAccessPoint current;
    bool inBlock = false;

    for (int i = 0; i < lines.size(); ++i) {
        const QString line = lines.at(i);
        const QRegularExpressionMatch bss = bssRx.match(line);
        if (bss.hasMatch()) {
            if (inBlock && !current.ssid.isEmpty())
                appendUnique(&parsed, current);
            current = WifiAccessPoint();
            current.bssid = bss.captured(1);
            inBlock = true;
            continue;
        }

        if (inBlock) {
            const QRegularExpressionMatch ssid = ssidRx.match(line);
            if (ssid.hasMatch()) {
                current.ssid = ssid.captured(1);
                continue;
            }
            const QRegularExpressionMatch sig = signalRx.match(line);
            if (sig.hasMatch()) {
                current.signalDbm = sig.captured(1).toInt();
                continue;
            }
            if (line.contains(QStringLiteral("RSN")) || line.contains(QStringLiteral("WPA")))
                current.secure = true;
            continue;
        }

        const QRegularExpressionMatch table = tableRx.match(line);
        if (table.hasMatch()) {
            WifiAccessPoint ap;
            ap.ssid = table.captured(1).trimmed();
            ap.signalDbm = table.captured(2).toInt();
            ap.secure = table.captured(3).contains(QStringLiteral("WPA"), Qt::CaseInsensitive);
            if (!ap.ssid.isEmpty() && ap.ssid.compare(QStringLiteral("SSID"), Qt::CaseInsensitive) != 0)
                appendUnique(&parsed, ap);
        }
    }
    if (inBlock && !current.ssid.isEmpty())
        appendUnique(&parsed, current);

    std::stable_sort(parsed.begin(), parsed.end(), strongerFirst);

    beginResetModel();
    m_points = parsed;
    endResetModel();
}
