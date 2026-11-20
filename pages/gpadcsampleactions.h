#ifndef GPADCSAMPLEACTIONS_H
#define GPADCSAMPLEACTIONS_H

// GPADC 按键动作的**示例实现**：切页三件套。
//
// 为什么单独一个文件、放在 pages/ 下：
//   GpadcService 只提供「动作表 + 绑定」这套机制，不认识任何业务
//   （services/ 不能依赖页面 / MainWindow）。切页是纯 UI 语义，
//   所以它的实现属于 pages/，由页面在构造时注册进服务。
//
// 蓝牙同事接入自己的动作时照这个文件的形状写就行：
//   1. 写一个 registerXxxActions()，内部调 GpadcService::instance()->addAction()；
//   2. 在自己模块初始化时调一次（例如对应页面的 buildContent()）；
//   3. 不改 gpadcservice.*，也不改 gpadcpage.* —— 下拉框会自动多出这些项。
//
// 为什么不在 GpadcService 构造时注册：
//   那会让服务层 include navigationbus（勉强可以）乃至更上层的 UI 语义。
//   动作是「谁来用」的事情，注册点放在使用方，依赖方向才保持向下。
void registerGpadcSampleActions();

#endif // GPADCSAMPLEACTIONS_H
