#ifndef HMI_CORE_PLAYERSTATE_H
#define HMI_CORE_PLAYERSTATE_H

#include <QString>

class MediaService;

// 设计模式 #12：状态（State，多态版）
//
// 播放器的行为随状态而变，把「每个状态下能做什么」放进独立的状态对象：
//   空闲 → 必须先选文件；播放中 → 再按播放无效；暂停 → 再按播放继续；错误 → 要重新 open。
// 于是 MediaService 里不再有一堆 if (state == ...) 判断，新增状态只加一个类。
class PlayerState
{
public:
    virtual ~PlayerState();

    virtual QString name() const = 0;

    // 返回 false 表示本状态不接受该动作（会通过 ctx.notify() 给出提示）
    virtual bool play(MediaService &ctx) = 0;
    virtual bool pause(MediaService &ctx) = 0;
    virtual bool stop(MediaService &ctx) = 0;

    virtual bool acceptsMedia() const;
};

class IdleState : public PlayerState
{
public:
    static PlayerState *instance();
    QString name() const override;
    bool play(MediaService &ctx) override;
    bool pause(MediaService &ctx) override;
    bool stop(MediaService &ctx) override;
};

class PlayingState : public PlayerState
{
public:
    static PlayerState *instance();
    QString name() const override;
    bool play(MediaService &ctx) override;
    bool pause(MediaService &ctx) override;
    bool stop(MediaService &ctx) override;
};

class PausedState : public PlayerState
{
public:
    static PlayerState *instance();
    QString name() const override;
    bool play(MediaService &ctx) override;
    bool pause(MediaService &ctx) override;
    bool stop(MediaService &ctx) override;
};

class ErrorState : public PlayerState
{
public:
    static PlayerState *instance();
    QString name() const override;
    bool play(MediaService &ctx) override;
    bool pause(MediaService &ctx) override;
    bool stop(MediaService &ctx) override;
    bool acceptsMedia() const override;
};

#endif // HMI_CORE_PLAYERSTATE_H
