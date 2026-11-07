#ifndef UPGRADEPAGE_H
#define UPGRADEPAGE_H

#include "basepage.h"
#include "upgradeservice.h"

class QLabel;
class QProgressBar;
class CardButton;

// U 盘升级页：检测介质 → 显示包内容 → 校验 → 覆盖安装 → 提示重启。
// 页面只做展示与触发，所有动作与状态都在 UpgradeService 里。
class UpgradePage : public BasePage
{
    Q_OBJECT
public:
    explicit UpgradePage(QWidget *parent = nullptr);

private slots:
    void onStateChanged(UpgradeService::State state);
    void onProgress(int percent, const QString &text);
    void refreshView();

private:
    void buildContent() override;
    void onEnter() override;

    QLabel       *m_info;         // 介质 / 版本 / 包内容合并成一个框，省纵向空间
    QProgressBar *m_progress;
    CardButton   *m_verifyBtn;
    CardButton   *m_installBtn;
    CardButton   *m_restartBtn;
    CardButton   *m_rollbackBtn;
};

#endif // UPGRADEPAGE_H
