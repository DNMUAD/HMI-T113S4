#ifndef MEDIAPAGE_H
#define MEDIAPAGE_H

#include "basepage.h"

class QComboBox;
class QSlider;
class QLabel;

// M4：QMediaPlayer + QVideoWidget（软解）。M1 保留播放控制与视频占位区。
class MediaPage : public BasePage
{
    Q_OBJECT
public:
    explicit MediaPage(QWidget *parent = nullptr);

protected:
    void buildContent() override;

private:
    void reloadMediaList();
    QComboBox *m_fileList;
    QSlider   *m_volume;
    QLabel    *m_state;
};

#endif // MEDIAPAGE_H
