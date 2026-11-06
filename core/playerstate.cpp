#include "playerstate.h"
#include "mediaservice.h"

#include <QMediaPlayer>

PlayerState::~PlayerState()
{
}

bool PlayerState::acceptsMedia() const
{
    return true;
}

// ---------------- IdleState ----------------
PlayerState *IdleState::instance()
{
    static IdleState state;
    return &state;
}

QString IdleState::name() const
{
    return QObject::tr("空闲");
}

bool IdleState::play(MediaService &ctx)
{
    if (!ctx.hasMedia()) {
        ctx.notify(QObject::tr("请先选择媒体文件"));
        return false;
    }
    if (ctx.player())
        ctx.player()->play();
    ctx.setState(PlayingState::instance());
    return true;
}

bool IdleState::pause(MediaService &ctx)
{
    ctx.notify(QObject::tr("当前没有正在播放的内容"));
    return false;
}

bool IdleState::stop(MediaService &ctx)
{
    Q_UNUSED(ctx);
    return true;                 // 已是空闲，停止是幂等操作
}

// ---------------- PlayingState ----------------
PlayerState *PlayingState::instance()
{
    static PlayingState state;
    return &state;
}

QString PlayingState::name() const
{
    return QObject::tr("播放中");
}

bool PlayingState::play(MediaService &ctx)
{
    Q_UNUSED(ctx);
    return true;                 // 已在播放，重复按下忽略
}

bool PlayingState::pause(MediaService &ctx)
{
    if (ctx.player())
        ctx.player()->pause();
    ctx.setState(PausedState::instance());
    return true;
}

bool PlayingState::stop(MediaService &ctx)
{
    if (ctx.player())
        ctx.player()->stop();
    ctx.setState(IdleState::instance());
    return true;
}

// ---------------- PausedState ----------------
PlayerState *PausedState::instance()
{
    static PausedState state;
    return &state;
}

QString PausedState::name() const
{
    return QObject::tr("已暂停");
}

bool PausedState::play(MediaService &ctx)
{
    if (ctx.player())
        ctx.player()->play();
    ctx.setState(PlayingState::instance());
    return true;
}

bool PausedState::pause(MediaService &ctx)
{
    Q_UNUSED(ctx);
    return true;
}

bool PausedState::stop(MediaService &ctx)
{
    if (ctx.player())
        ctx.player()->stop();
    ctx.setState(IdleState::instance());
    return true;
}

// ---------------- ErrorState ----------------
PlayerState *ErrorState::instance()
{
    static ErrorState state;
    return &state;
}

QString ErrorState::name() const
{
    return QObject::tr("错误");
}

bool ErrorState::acceptsMedia() const
{
    return true;                 // 错误后允许换一个文件重新开始
}

bool ErrorState::play(MediaService &ctx)
{
    ctx.notify(QObject::tr("播放器处于错误状态，请重新选择文件"));
    return false;
}

bool ErrorState::pause(MediaService &ctx)
{
    Q_UNUSED(ctx);
    return false;
}

bool ErrorState::stop(MediaService &ctx)
{
    if (ctx.player())
        ctx.player()->stop();
    ctx.setState(IdleState::instance());
    return true;
}
