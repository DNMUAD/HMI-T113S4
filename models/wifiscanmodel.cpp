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
// wpa_cli scan_results：
//   bssid / frequency / signal level / flags / ssid
// 真实输出是 TAB 分隔（wpa_supplicant 里的格式串是 "%ld\t%s\t..."），
// 但串口终端会把 TAB 展开成空格、日志里也可能被人按空格贴过来，
// 所以分隔符用 [ \t]+ 通吃两种形态 —— 否则「usable 判为可用、解析却是 0 个」，
// 界面就会退回到「扫描完成，但未解析到 SSID」那句误导性提示。
const char *kWpaCliPattern = "^([0-9a-fA-F:]{17})[ \\t]+(\\d+)[ \\t]+(-?\\d+)[ \\t]+(\\S+)"
                             "(?:[ \\t]+(.*))?$";

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

// iw / wpa_cli 打印 SSID 时，非 ASCII 字节一律转义成 \xNN（iw 里是
// iw_ssid_escape：可打印字符原样输出，其余吐 \xHH）。中文热点因此显示成
//   \xe8\x8f\xa0\xe8\x90\x9d\xe6\x89\x8b\xe6\x9c\xba        (= 菠萝手机)
// 这串东西如果不去还原，就会被当成 SSID 字面量拿去连接，必然连不上。
//
// 已知局限：iw 对「本身含反斜杠」的 SSID 不做二次转义（反斜杠是可打印字符），
// 所以名为 "\\x41" 的热点会被还原成 "A"。802.11 实务中不存在这种名字，
// 不值得为它增加格式歧义。
//
// 整段解码而不是只解 SSID 字段：实现更简单，且 \xNN 在输出里只可能来自转义。
QString WifiScanModel::decodeEscaped(const QString &raw)
{
    if (!raw.contains(QStringLiteral("\\x")))
        return raw;

    const QStringList lines = raw.split(QLatin1Char('\n'));
    QStringList out;
    out.reserve(lines.size());
    for (int n = 0; n < lines.size(); ++n) {
        const QString line = lines.at(n);
        if (!line.contains(QStringLiteral("\\x"))) {
            out.append(line);
            continue;
        }

        QByteArray bytes;
        bytes.reserve(line.size());
        for (int i = 0; i < line.size(); ++i) {
            const QChar c = line.at(i);
            if (c == QLatin1Char('\\') && i + 3 < line.size()
                    && (line.at(i + 1) == QLatin1Char('x') || line.at(i + 1) == QLatin1Char('X'))) {
                bool ok = false;
                const int v = line.mid(i + 2, 2).toInt(&ok, 16);
                if (ok) {
                    bytes.append(static_cast<char>(v));
                    i += 3;
                    continue;
                }
            }
            // 非转义字符：raw 是按字节收进来的（Latin-1 等价），原样取低字节
            bytes.append(c.toLatin1());
        }

        // 现代 AP 的 SSID 基本都是 UTF-8。若不是合法 UTF-8（老设备可能用 GBK），
        // 退回 Latin-1：字节无损，QProcess 传参时也按本地编码还原，
        // 保证「显示可能仍不好看，但连接用的字节是对的」。
        QString decoded = QString::fromUtf8(bytes);
        if (decoded.contains(QChar(QChar::ReplacementCharacter)))
            decoded = QString::fromLatin1(bytes);
        out.append(decoded);
    }
    return out.join(QLatin1Char('\n'));
}

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

void WifiScanModel::setRawOutput(const QString &rawText)
{
    QList<WifiAccessPoint> parsed;

    // 先把 \xNN 还原成真实字节（中文 SSID 必需），再走各格式的解析。
    // 必须在解析前做：否则 SSID 会带着 \xe8... 字面量进列表，连接时也对不上。
    const QString raw = decodeEscaped(rawText);

    const QRegularExpression bssRx(QString::fromLatin1(kBssPattern));
    const QRegularExpression ssidRx(QString::fromLatin1(kSsidPattern));
    const QRegularExpression signalRx(QString::fromLatin1(kSignalPattern));
    const QRegularExpression tableRx(QString::fromLatin1(kTablePattern));
    const QRegularExpression wpaCliRx(QString::fromLatin1(kWpaCliPattern));

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

        // wpa_cli scan_results：整行独立，没有 BSS 块结构
        const QRegularExpressionMatch wpa = wpaCliRx.match(line);
        if (wpa.hasMatch()) {
            WifiAccessPoint ap;
            ap.bssid = wpa.captured(1);
            ap.signalDbm = wpa.captured(3).toInt();
            // flags 里出现 WPA/WEP 即视为加密；[ESS] 单独出现是开放网络
            const QString flags = wpa.captured(4);
            ap.secure = flags.contains(QStringLiteral("WPA"), Qt::CaseInsensitive)
                        || flags.contains(QStringLiteral("WEP"), Qt::CaseInsensitive);
            // 第 5 组可能不存在（隐藏网络整行没有 ssid 字段）
            ap.ssid = wpa.captured(5).trimmed();
            if (!ap.ssid.isEmpty())
                appendUnique(&parsed, ap);
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
