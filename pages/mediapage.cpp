#include "mediapage.h"
#include "mediaservice.h"
#include "pagefactory.h"

#include <QComboBox>
#include <QDir>
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>

MediaPage::MediaPage(QWidget *parent)
    : BasePage(tr("多媒体"), parent)
    , m_fileList(nullptr)
    , m_volume(nullptr)
    , m_state(nullptr)
{
}

void MediaPage::buildContent()
{
    // 硬约定：界面标注「软解」
    setHint(tr("软解：无硬解，仅 MP3 与 ≤720p H.264 可流畅播放，1080p 会卡。"
               " QVideoWidget 待 M4 接入（届时 .pro 追加 QT += multimediawidgets）。"));

    QFrame *video = new QFrame(this);
    video->setObjectName(QStringLiteral("videoPlaceholder"));
    // 占位区固定高度：原来 200px 把下面的播放按钮全挤出可视区。
    // M4 接入真实 QVideoWidget 后改成随窗口伸缩即可。
    video->setFixedHeight(120);
    video->setFrameShape(QFrame::Box);
    QLabel *ph = new QLabel(tr("视频输出区（M4 接 QVideoWidget）"), video);
    ph->setObjectName(QStringLiteral("videoPlaceholderText"));   // 颜色单独给，QSS 不继承到子控件
    ph->setAlignment(Qt::AlignCenter);
    QGridLayout *phLayout = new QGridLayout(video);
    phLayout->addWidget(ph, 0, 0, Qt::AlignCenter);
    addWidget(video);

    m_fileList = new QComboBox(this);
    addWidget(m_fileList);
    reloadMediaList();

    m_volume = new QSlider(Qt::Horizontal, this);
    m_volume->setRange(0, 100);
    m_volume->setValue(70);
    connect(m_volume, &QSlider::valueChanged, this, [](int v) {
        MediaService::instance()->setVolume(v);
    });
    addWidget(m_volume);

    m_state = new QLabel(tr("就绪"), this);
    addWidget(m_state);

    QGridLayout *grid = addGrid();
    grid->addWidget(newButton(tr("播放"), [this]() {
        MediaService::instance()->open(m_fileList->currentData().toString());
    }, tr("打开当前选中文件"), true), 0, 0);
    grid->addWidget(newButton(tr("暂停"), []() { MediaService::instance()->pause(); }, tr("暂停/继续")), 0, 1);
    grid->addWidget(newButton(tr("停止"), []() { MediaService::instance()->stop(); }, tr("停止并释放")), 1, 0);
    grid->addWidget(newButton(tr("刷新列表"), [this]() { reloadMediaList(); }, tr("扫描 U 盘 / /usr/share")), 1, 1);

    connect(MediaService::instance(), &MediaService::stateChanged, this,
            [this](const QString &state, const QString &detail) {
        m_state->setText(detail.isEmpty() ? state : state + QStringLiteral(" · ") + detail);
    });
}

void MediaPage::reloadMediaList()
{
    m_fileList->clear();
    const QStringList filters = QStringList() << QStringLiteral("*.mp3") << QStringLiteral("*.mp4")
                                              << QStringLiteral("*.wav") << QStringLiteral("*.avi");
    for (const QString &root : MediaService::mediaRoots()) {
        QDir dir(root);
        if (!dir.exists())
            continue;
        const QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);
        for (const QFileInfo &fi : files)
            m_fileList->addItem(fi.fileName(), fi.absoluteFilePath());
    }
    if (m_fileList->count() == 0)
        m_fileList->addItem(tr("（未找到媒体文件）"), QString());
}

HMI_REGISTER_PAGE("media", 30, QT_TR_NOOP("多媒体"), MediaPage)
