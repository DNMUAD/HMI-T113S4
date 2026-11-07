#ifndef HMI_WIDGETS_UIMETRICS_H
#define HMI_WIDGETS_UIMETRICS_H

// 界面尺寸的唯一来源（目标屏 1024x600，96dpi）。
//
// 之前这些数字散落在 mainwindow.cpp / cardbutton.cpp / basepage.cpp / style.qss 里，
// 各改各的：导航 180 + 内容区 minimumWidth 844 + 边距 16 = 1040 > 1024，
// 窗口被硬撑开，控件比例整体失调。现在代码侧统一引用这里，
// style.qss 里对应的 px 值在文件头列了对照表，改尺寸记得两边一起改。
namespace UiMetrics {

// ---- 左导航 ----
const int NavWidth       = 168;
const int NavListWidth   = 152;
const int NavItemHeight  = 44;   // ≥44px，仍是可触摸的最小目标
const int NavItemGap     = 2;

// ---- 页面内容 ----
const int PageMarginH    = 14;
const int PageMarginTop  = 10;
const int PageMarginBot  = 8;
const int PageSpacing    = 7;

// ---- 控件 ----
const int ButtonHeight        = 42;   // 单行按钮 / 输入框
const int ButtonHeightTwoLine = 58;   // 标题 + 说明两行
const int ButtonMinWidth      = 120;
const int StatusHeight        = 24;   // 页面状态行最小高度

// ---- 字号（pt）----
const int TitlePointSize  = 18;   // 页面标题
const int TextPointSize   = 14;   // 正文 / 按钮标题
const int DescPointSize   = 10;   // 按钮副标题
const int HintPointSize   = 12;   // 常驻提示条
const int StatusPointSize = 13;   // 页面状态行
const int LogPointSize    = 10;   // 日志正文

// ---- 日志 / 状态栏 ----
const int LogHeight       = 114;
const int LogButtonHeight = 28;   // 日志条上的「清空」按钮，非高频操作，可以小
const int StatusBarHeight = 30;

} // namespace UiMetrics

#endif // HMI_WIDGETS_UIMETRICS_H
