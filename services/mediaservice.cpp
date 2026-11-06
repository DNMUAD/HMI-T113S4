#include "mediaservice.h"
#include "appsettings.h"
#include "logpanel.h"
#include "playerstate.h"
#include "serviceregistry.h"
#include "singleton.h"

#include <QDir>
#include <QFileInfo>
#include <QMediaPlayer>
#include <QUrl>

MediaService *MediaService::instance()
{
    return hmi::singleton<MediaService>([]() { return new MediaService; });
}

MediaService::MediaService(QObject *parent)
    : DeviceService(QStringLiteral("media"), QObject::tr("多媒体"), 0, parent)
    , m_player(new QMediaPlayer(this))
    , m_state(IdleState::instance())
    , m_hasMedia(false)
{
    // QMediaPlayer 同时存在 Error error() const 取值函数与 void error(Error) 信号，
    // 必须用 QOverload 消歧（Qt 5.7+）。
    connect(m_player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this,
            [this](QMediaPlayer::Error) {
        // M4 硬约定：gstreamer / 后端错误必须进日志
        LogPanel::post(QStringLiteral("媒体播放错误: %1").arg(m_player->errorString()), LogPanel::Error);
        setState(ErrorState::instance(), m_player->errorString());
    });
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this,
            [this](QMediaPlayer::MediaStatus st) { onMediaStatusChanged(int(st)); });
}

void MediaService::onMediaStatusChanged(int status)
{
    if (status == QMediaPlayer::InvalidMedia) {
        setState(ErrorState::instance(), tr("媒体格式不支持或文件损坏"));
        return;
    }
    if (status == QMediaPlayer::EndOfMedia) {
        setState(IdleState::instance(), tr("播放结束"));
        return;
    }
}

QMediaPlayer *MediaService::player() const
{
    return m_player;
}

bool MediaService::hasMedia() const
{
    return m_hasMedia;
}

PlayerState *MediaService::state() const
{
    return m_state;
}

QStringList MediaService::mediaRoots()
{
    return AppSettings::instance()->mediaRoots();
}

void MediaService::open(const QString &path)
{
    if (path.isEmpty()) {
        LogPanel::post(QStringLiteral("媒体路径为空"), LogPanel::Warn);
        notify(tr("请先选择媒体文件"));
        return;
    }
    m_player->setMedia(QUrl::fromLocalFile(path));
    m_hasMedia = true;
    LogPanel::post(QStringLiteral("加载媒体: %1").arg(QFileInfo(path).fileName()), LogPanel::Info);
    play();
}

void MediaService::play()
{
    m_state->play(*this);          // 状态对象决定「能不能播、播了之后变什么状态」
}

void MediaService::pause()
{
    m_state->pause(*this);
}

void MediaService::stop()
{
    m_state->stop(*this);
}

void MediaService::setVolume(int percent)
{
    m_player->setVolume(qBound(0, percent, 100));
}

void MediaService::seek(qint64 ms)
{
    m_player->setPosition(ms);
}

void MediaService::setState(PlayerState *state, const QString &detail)
{
    if (!state)
        return;

    m_state = state;
    emit stateChanged(m_state->name(), detail);

    const DeviceState deviceState = (m_state == ErrorState::instance()) ? DeviceError
                                  : (m_state == IdleState::instance())  ? DeviceOff
                                                                        : DeviceReady;
    publish(deviceState, detail.isEmpty() ? m_state->name()
                                          : m_state->name() + QStringLiteral(" · ") + detail);
}

void MediaService::notify(const QString &message)
{
    LogPanel::post(message, LogPanel::Warn);
    emit stateChanged(m_state->name(), message);
}

HMI_REGISTER_SERVICE("media", MediaService)
