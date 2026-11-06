#include "settingview.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QSlider>
#include <QVBoxLayout>

SettingView::SettingView(QWidget *parent)
    : BaseView(parent)
{
    setObjectName(QStringLiteral("settingView"));

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(48, 40, 48, 40);
    layout->setSpacing(16);

    QLabel *title = new QLabel(QStringLiteral("界面控件示例"), this);
    title->setObjectName(QStringLiteral("pageTitle"));

    QGroupBox *group = new QGroupBox(QStringLiteral("表单区域"), this);
    QFormLayout *form = new QFormLayout(group);
    form->setContentsMargins(16, 24, 16, 16);
    form->setSpacing(12);

    QLineEdit *edit = new QLineEdit(group);
    edit->setPlaceholderText(QStringLiteral("在这里输入内容"));

    QComboBox *combo = new QComboBox(group);
    combo->addItems(QStringList() << QStringLiteral("选项一")
                                  << QStringLiteral("选项二")
                                  << QStringLiteral("选项三"));

    QSlider *slider = new QSlider(Qt::Horizontal, group);
    slider->setRange(0, 100);
    slider->setValue(40);

    QCheckBox *check = new QCheckBox(QStringLiteral("启用某项功能"), group);

    form->addRow(QStringLiteral("文本输入"), edit);
    form->addRow(QStringLiteral("下拉选择"), combo);
    form->addRow(QStringLiteral("数值调节"), slider);
    form->addRow(QString(), check);

    layout->addWidget(title);
    layout->addWidget(group);
    layout->addStretch(1);
}

QString SettingView::title() const
{
    return QStringLiteral("设置");
}
