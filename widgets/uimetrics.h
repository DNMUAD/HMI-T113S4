#ifndef HMI_WIDGETS_UIMETRICS_H
#define HMI_WIDGETS_UIMETRICS_H

// 界面尺寸的唯一来源（板端 800x480，桌面调试 1024x600，96dpi）。
//
// 之前这些数字散落在 mainwindow.cpp / cardbutton.cpp / basepage.cpp / style.qss 里，
// 各改各的：导航 180 + 内容区 minimumWidth 844 + 边距 16 = 1040 > 1024，
// 窗口被硬撑开，控件比例整体失调。现在代码侧统一引用这里，
// style.qss 里对应的 px 值在文件头列了对照表，改尺寸记得两边一起改。
namespace UiMetrics {

// ---- 左导航 ----
const int NavWidth        = 168;
const int NavListWidth    = 152;
const int NavCollapsedWidth = 80;   // 收起后的窄条：只留 ☰ 与首字导航
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

// ---- 字号（px）----
// 板端 800x480 是固定分辨率的嵌入式屏，字号必须用像素锚定：
// 用 pt 时 Qt 会按物理 DPI 换算（7 寸屏约 130-190dpi，14pt ≈ 37px），
// 而按钮/导航高度是固定 px，文字会直接撑破控件。改用 px 后两边同一坐标系。
const int TitlePixelSize  = 22;   // 页面标题
const int TextPixelSize   = 17;   // 正文 / 按钮标题
const int DescPixelSize   = 12;   // 按钮副标题
const int HintPixelSize   = 15;   // 常驻提示条
const int StatusPixelSize = 16;   // 页面状态行
const int LogPixelSize    = 12;   // 日志正文
const int NavPixelSize    = 17;   // 导航项
const int ClockPixelSize  = 14;   // 状态栏时钟与状态灯

// 旧名保留一处引用点（verify.py 校验 TitlePointSize 存在）
const int TitlePointSize  = TitlePixelSize;

// ---- 软键盘 ----
// 800x480 屏：4 行键（3 行字符 + 1 行功能键）占 185px，约 38% 屏高，
// 剩下的画布仍能看到输入框本身。键高 40px 略小于按钮 42px，
// 因为键盘一次要排 10 列，横向单键只剩 (800-16)/10 ≈ 78px，等比缩小更好看。
const int SoftKeyHeight   = 40;
const int SoftKeySpacing  = 3;
const int SoftKeyMargin   = 8;
const int SoftKeyColumns  = 10;          // 字符行固定 10 列，所有布局按此对齐
const int SoftKeyboardHeight = SoftKeyHeight * 4 + SoftKeySpacing * 3 + SoftKeyMargin * 2;

// ---- 日志 / 状态栏 ----
const int LogHeight       = 114;
const int LogHeightCompact = 84;  // 480 高的屏幕上日志让位给功能区
const int LogButtonHeight = 28;   // 日志条上的「清空」按钮，非高频操作，可以小
const int StatusBarHeight = 30;

// ---- 窗口 ----
const int MinWindowWidth  = 800;  // 板端显示屏实际分辨率的下限
const int MinWindowHeight = 480;
const int AutoCollapseBelow = 900; // 窗口宽度低于此值，导航默认收起

} // namespace UiMetrics

#endif // HMI_WIDGETS_UIMETRICS_H
