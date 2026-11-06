#ifndef BTSERVICE_H
#define BTSERVICE_H

#include "deviceservice.h"

#include <QString>

// 蓝牙页骨架：M1 只做控制器状态查询，配对/扫描留待后续里程碑。
class BtService : public DeviceService
{
    Q_OBJECT
public:
    static BtService *instance();

    void refresh() override;
    void powerOn();      // TODO(M6)
    void powerOff();     // TODO(M6)
    void scan();         // TODO(M6)

private slots:
    void onFinished(int jobId, int exitCode, int exitStatus, const QString &out);

private:
    explicit BtService(QObject *parent = nullptr);
    int m_statusJob;
};

#endif // BTSERVICE_H
