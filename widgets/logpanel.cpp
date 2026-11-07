#include "logpanel.h"
#include "singleton.h"
#include "uimetrics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QMetaObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCharFormat>
#include <QVBoxLayout>

LogPanel *LogPanel::instance()
{
    // 无 parent：MainWindow 加入布局时 Qt 自动 reparent。
    // 单例实现统一走 core/singleton.h，不再每处手写「静态指针 + 判空」。
    return hmi::singleton<LogPanel>([]() { return new LogPanel; });
}

void LogPanel::post(const QString &message, Level level)
{
    LogPanel *panel = instance();
    // QueuedConnection 保证跨线程调用也只在 GUI 线程触碰控件。
    QMetaObject::invokeMethod(panel, "appendLine", Qt::QueuedConnection,
                              Q_ARG(QString, message), Q_ARG(int, int(level)));
}

LogPanel::LogPanel(QWidget *parent)
    : QWidget(parent)
    , m_view(nullptr)
    , m_clearButton(nullptr)
{
    setObjectName(QStringLiteral("logPanel"));

    m_view = new QPlainTextEdit(this);
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setMaximumBlockCount(500);   // 硬约定：只读，限 500 行
    m_view->setObjectName(QStringLiteral("logView"));

    m_clearButton = new QPushButton(tr("清空"), this);
    m_clearButton->setObjectName(QStringLiteral("logClearButton"));
    m_clearButton->setFixedHeight(UiMetrics::LogButtonHeight);
    m_clearButton->setMaximumWidth(76);
    connect(m_clearButton, &QPushButton::clicked, this, &LogPanel::clear);

    QWidget *header = new QWidget(this);
    QHBoxLayout *hl = new QHBoxLayout(header);
    hl->setContentsMargins(2, 0, 0, 0);
    hl->setSpacing(6);
    QLabel *title = new QLabel(tr("运行日志（命令输出 / stderr）"), header);
    title->setObjectName(QStringLiteral("logTitle"));
    hl->addWidget(title, 1);
    hl->addWidget(m_clearButton);

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(4, 2, 4, 2);
    root->setSpacing(2);
    root->addWidget(header);
    root->addWidget(m_view, 1);

    setMinimumHeight(UiMetrics::LogHeight);
}

void LogPanel::setMaxLines(int lines)
{
    if (lines > 0)
        m_view->setMaximumBlockCount(lines);
}

int LogPanel::maxLines() const
{
    return m_view->maximumBlockCount();
}

void LogPanel::clear()
{
    m_view->clear();
}

void LogPanel::appendLine(const QString &message, int level)
{
    static const char *tags[] = { "[INFO]", "[ OK ]", "[WARN]", "[FAIL]", "[DBG]" };
    const int idx = qBound(0, level, 4);

    QColor color = palette().color(QPalette::Text);
    switch (level) {
    case Ok:    color = QColor(QStringLiteral("#1E8E3E")); break;
    case Warn:  color = QColor(QStringLiteral("#B06000")); break;
    case Error: color = QColor(QStringLiteral("#C62828")); break;
    case Debug: color = QColor(QStringLiteral("#6B778C")); break;
    default: break;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"));
    const QStringList lines = message.split(QLatin1Char('\n'), QString::SkipEmptyParts);
    for (const QString &line : lines) {
        QTextCharFormat fmt;
        fmt.setForeground(color);
        m_view->setCurrentCharFormat(fmt);
        m_view->appendPlainText(QStringLiteral("%1 %2 %3").arg(stamp, QString::fromLatin1(tags[idx]), line.trimmed()));
    }
}
