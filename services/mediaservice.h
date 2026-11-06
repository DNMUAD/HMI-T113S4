#ifndef MEDIASERVICE_H
#define MEDIASERVICE_H

#include "deviceservice.h"

#include <QString>
#include <QStringList>

class QMediaPlayer;
class PlayerState;

// QMediaPlayer + QVideoWidget（M4）。现实边界：无硬解，MP3 与 ≤720p H.264，1080p 会卡。
//
// State 模式上下文：play/pause/stop 不自己判断状态，转交给当前 PlayerState 对象；
// setState() 是给状态对象回调的「上下文接口」，也是唯一发 stateChanged 的地方。
class MediaService : public DeviceService
{
    Q_OBJECT
public:
    static MediaService *instance();

    QMediaPlayer *player() const;          // 供 MediaPage 绑定 QVideoWidget
    static QStringList mediaRoots();       // U 盘 / /usr/share

    void open(const QString &path);
    void play();
    void pause();
    void stop();
    void setVolume(int percent);
    void seek(qint64 ms);

    bool hasMedia() const;
    PlayerState *state() const;

    // 供状态对象回调（State 模式的 Context 接口）
    void setState(PlayerState *state, const QString &detail = QString());
    void notify(const QString &message);

signals:
    void stateChanged(const QString &state, const QString &detail);

private:
    explicit MediaService(QObject *parent = nullptr);
    void onMediaStatusChanged(int status);

    QMediaPlayer *m_player;
    PlayerState  *m_state;
    bool          m_hasMedia;
};

#endif // MEDIASERVICE_H
