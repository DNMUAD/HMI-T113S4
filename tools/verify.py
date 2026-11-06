#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""HMI-T113S4 工程结构静态校验。

本机若没有 Qt5/qmake（例如只有 Qt 4.8.7），无法真编译，可用本脚本先做
「文件齐备 / moc 覆盖 / include 闭合 / 硬约定」检查：

    python tools/verify.py

退出码 0 = 全部通过；1 = 存在错误。警告不阻断。
"""
import os
import re
import sys
import xml.etree.ElementTree as ET

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
errors = []
warnings = []


def err(msg):
    errors.append(msg)


def warn(msg):
    warnings.append(msg)


def abspath(*parts):
    return os.path.join(ROOT, *parts)


def read(path):
    with open(path, 'r', encoding='utf-8') as f:
        return f.read()


def logical_lines(text):
    """合并 qmake 的反斜杠续行，去掉行注释。"""
    buf = ''
    for raw in text.splitlines():
        line = raw.split('#', 1)[0].rstrip()
        if line.endswith('\\'):
            buf += line[:-1].rstrip() + ' '
            continue
        buf += line
        if buf.strip():
            yield buf.strip()
        buf = ''
    if buf.strip():
        yield buf.strip()


def parse_qmake(path, sections, seen):
    path = os.path.normpath(path)
    if path in seen:
        return
    seen.add(path)
    if not os.path.isfile(path):
        err('缺失 qmake 文件: %s' % path)
        return
    base = os.path.dirname(path)
    for line in logical_lines(read(path)):
        m = re.match(r'^include\(\s*([^)]+)\s*\)', line)
        if m:
            inc = m.group(1).strip().strip('"').replace('$$PWD', base)
            parse_qmake(os.path.normpath(os.path.join(base, inc)), sections, seen)
            continue
        m = re.match(r'^(SOURCES|HEADERS|RESOURCES|TRANSLATIONS|FORMS)\s*\+?=\s*(.*)$', line)
        if not m:
            continue
        key, rest = m.group(1), m.group(2)
        for token in rest.split():
            token = token.strip().strip('"').replace('$$PWD', base)
            if not token or token.startswith('$'):
                continue
            full = token if os.path.isabs(token) else os.path.normpath(os.path.join(ROOT, token))
            sections[key].append(full)
            if not os.path.isfile(full):
                err('%s 引用了不存在的文件: %s (来自 %s)' % (key, token, os.path.basename(path)))


def strip_cpp(text):
    """粗略去掉字符串/字符字面量与注释，便于做括号配对。"""
    out = []
    i, n = 0, len(text)
    state = 'code'
    while i < n:
        c = text[i]
        nx = text[i + 1] if i + 1 < n else ''
        if state == 'code':
            if c == '/' and nx == '/':
                state = 'line'
                i += 2
                continue
            if c == '/' and nx == '*':
                state = 'block'
                i += 2
                continue
            if c == '"':
                state = 'str'
                i += 1
                continue
            if c == "'":
                state = 'chr'
                i += 1
                continue
            out.append(c)
            i += 1
        elif state == 'line':
            if c == '\n':
                state = 'code'
                out.append(c)
            i += 1
        elif state == 'block':
            if c == '*' and nx == '/':
                state = 'code'
                i += 2
            else:
                if c == '\n':
                    out.append(c)
                i += 1
        elif state in ('str', 'chr'):
            if c == '\\':
                i += 2
                continue
            if (state == 'str' and c == '"') or (state == 'chr' and c == "'"):
                state = 'code'
            i += 1
    return ''.join(out)


def main():
    pro = abspath('HMI-T113S4.pro')
    sections = {k: [] for k in ('SOURCES', 'HEADERS', 'RESOURCES', 'TRANSLATIONS', 'FORMS')}
    parse_qmake(pro, sections, set())

    # 1) 硬约定：QT 模块
    pro_text = read(pro)
    qt_line = re.search(r'^QT\s*\+?=\s*(.+)$', pro_text, re.M)
    if not qt_line:
        err('.pro 缺少 QT += 声明')
    else:
        mods = set(qt_line.group(1).split())
        need = {'core', 'gui', 'widgets', 'multimedia', 'network', 'serialport'}
        missing = need - mods
        if missing:
            err('QT 模块缺失: %s' % ', '.join(sorted(missing)))
    if 'CONFIG += c++11' not in pro_text and 'CONFIG += c++1' not in pro_text:
        err('.pro 未启用 C++11')
    if not re.search(r'^TARGET\s*=\s*HMI-T113S4\s*$', pro_text, re.M):
        err('TARGET 必须保留 HMI-T113S4（改名只在 buildroot 安装行做）')

    # 2) 所有含 Q_OBJECT 的头文件必须列入 HEADERS（否则 moc 不生成）
    declared = {os.path.basename(p) for p in sections['HEADERS']}
    all_headers = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in ('.git', 'build')]
        for fn in filenames:
            if fn.endswith('.h'):
                all_headers.append(os.path.join(dirpath, fn))
    for h in all_headers:
        text = read(h)
        name = os.path.basename(h)
        if 'Q_OBJECT' in text and name not in declared:
            err('含 Q_OBJECT 但未列入 HEADERS（moc 不会生成）: %s' % os.path.relpath(h, ROOT))
        if not re.search(r'^#ifndef\s+\w+', text, re.M) or not re.search(r'^#define\s+\w+', text, re.M):
            err('头文件缺少 include guard: %s' % os.path.relpath(h, ROOT))

    # 3) 本地 #include "xxx.h" 必须存在
    known = {os.path.basename(h) for h in all_headers}
    for src in sections['SOURCES'] + all_headers:
        if not os.path.isfile(src):
            continue
        for inc in re.findall(r'#include\s+"([^"]+)"', read(src)):
            if os.path.basename(inc) not in known:
                err('%s 引用了不存在的本地头: %s' % (os.path.relpath(src, ROOT), inc))

    # 4) qrc 资源文件存在
    for qrc in sections['RESOURCES']:
        if not os.path.isfile(qrc):
            continue
        try:
            tree = ET.parse(qrc)
        except ET.ParseError as e:
            err('qrc XML 解析失败 %s: %s' % (os.path.relpath(qrc, ROOT), e))
            continue
        for node in tree.getroot().iter('file'):
            if node.text:
                target = os.path.join(os.path.dirname(qrc), node.text)
                if not os.path.isfile(target):
                    err('qrc 引用了不存在的文件: %s' % node.text)

    # 5) .ts 存在且 XML 合法
    for ts in sections['TRANSLATIONS']:
        if not os.path.isfile(ts):
            continue
        try:
            ET.parse(ts)
        except ET.ParseError as e:
            err('.ts XML 解析失败 %s: %s' % (os.path.relpath(ts, ROOT), e))

    # 6) 括号配对（去字符串/注释后）
    for src in sections['SOURCES']:
        if not os.path.isfile(src):
            continue
        clean = strip_cpp(read(src))
        for op, cl, label in (('{', '}', '花括号'), ('(', ')', '圆括号')):
            if clean.count(op) != clean.count(cl):
                err('%s %s 不配对 (%d vs %d)' % (os.path.relpath(src, ROOT), label,
                                                 clean.count(op), clean.count(cl)))

    # 7) 硬约定：外部命令只能经 ProcessRunner，禁止阻塞 UI
    banned = re.compile(r'waitForFinished|waitForStarted|QProcess::execute|\bsystem\s*\(|\bpopen\s*\(|QThread::(sleep|msleep|usleep)')
    for folder in ('pages', 'services', 'widgets', 'core', 'models'):
        for dirpath, _, filenames in os.walk(abspath(folder)):
            for fn in filenames:
                if not fn.endswith('.cpp'):
                    continue
                p = os.path.join(dirpath, fn)
                for mo in banned.finditer(read(p)):
                    err('禁止的阻塞/直调 API: %s in %s' % (mo.group(0), os.path.relpath(p, ROOT)))
                if fn not in ('processcommandrunner.cpp',) and re.search(r'\bnew\s+QProcess\b', read(p)):
                    err('外部命令必须走 ProcessRunner，禁止直接 new QProcess: %s' % os.path.relpath(p, ROOT))

    # QProcess 细节只允许出现在适配器里；日志只在装饰器里
    adapter = read(abspath('core', 'processcommandrunner.cpp'))
    for token in ('MergedChannels', 'setSingleShot', 'emit failed', 'emit finished'):
        if token not in adapter:
            err('ProcessCommandRunner（QProcess 适配器）缺少关键实现: %s' % token)
    decorator = read(abspath('core', 'loggingcommandrunner.cpp'))
    if 'LogPanel::Error' not in decorator:
        err('LoggingCommandRunner 必须把失败输出以 Error 级别打进 LogPanel')
    if 'qBound(3000' not in adapter:
        err('执行器未做 3–5s 超时钳制')

    runner = read(abspath('services', 'processrunner.cpp'))
    if not re.search(r'qBound\s*\(\s*3000\s*,\s*\w+\s*,\s*5000\s*\)', runner):
        warn('ProcessRunner 未找到 3–5s 超时钳制（请人工复核）')
    if 'installBackend' not in read(abspath('services', 'processrunner.h')):
        err('ProcessRunner 应保留 installBackend() 注入策略执行器')

    # 8) 触摸 / 日志 / 布局约定
    checks = [
        ('widgets/cardbutton.cpp', 'setMinimumHeight(56)', '按钮高 ≥56px'),
        ('widgets/logpanel.cpp', 'setMaximumBlockCount(500)', 'LogPanel 限 500 行'),
        ('widgets/logpanel.cpp', 'setReadOnly(true)', 'LogPanel 只读'),
        ('mainwindow.cpp', 'setFixedWidth(180)', '左导航 180px'),
        ('mainwindow.cpp', 'setMinimumWidth(844)', '内容区 844px 基准'),
        ('mainwindow.cpp', 'setSizeHint(QSize(164, 56))', '导航项 ≥56px'),
        ('main.cpp', '/etc/hmi.ini', 'QSettings /etc/hmi.ini'),
        ('main.cpp', 'setPointSize(', '全局字号（不硬编码字体族）'),
        ('main.cpp', 'QLocalServer', '单实例'),
        ('mainwindow.cpp', 'new StatusDot', '状态栏状态灯'),
        ('main.cpp', 'installBackend', '策略执行器注入点'),
        ('mainwindow.cpp', 'PageFactory', '页面来自注册表工厂'),
        ('mainwindow.cpp', 'StatusBus', '状态来自观察者总线'),
    ]
    for f, token, label in checks:
        if token not in read(abspath(*f.split('/'))):
            err('硬约定未落实: %s（%s 中缺少 %s）' % (label, f, token))
    if 'setFontFamily' in pro_text or 'setFamily' in read(abspath('main.cpp')):
        err('代码里不得硬编码字体族（中文靠 wqy-zenhei + fontconfig）')

    # 9) 页面 / 服务自注册：9 项导航 + 10 个页面 + 8 个服务
    page_files = [f for f in os.listdir(abspath('pages'))
                  if f.endswith('page.h') and f != 'basepage.h']
    if len(page_files) != 10:
        err('pages/ 应有 10 个页面头文件（不含 basepage.h），实际 %d' % len(page_files))

    registrations = []
    for fn in sorted(os.listdir(abspath('pages'))):
        if not fn.endswith('page.cpp') or fn == 'basepage.cpp':
            continue
        text = read(abspath('pages', fn))
        if 'HMI_REGISTER_PAGE(' not in text:
            err('页面未通过 PageFactory 自注册（MainWindow 不会创建它）: pages/%s' % fn)
            continue
        m = re.search(r'HMI_REGISTER_PAGE\(\s*"([^"]+)"\s*,\s*(-?\d+)\s*,\s*'
                      r'(nullptr|QT_TR_NOOP\("([^"]*)"\))\s*,\s*(\w+)\s*\)', text)
        if not m:
            err('HMI_REGISTER_PAGE 参数格式不符: pages/%s' % fn)
            continue
        registrations.append((m.group(1), int(m.group(2)), m.group(4), m.group(5)))

    nav_count = sum(1 for r in registrations if r[2] is not None)
    if len(registrations) != 10:
        err('自注册页面应为 10 个，实际 %d' % len(registrations))
    if nav_count != 9:
        err('左导航应为 9 项（总览页不占导航），实际 %d 项' % nav_count)
    ids = [r[0] for r in registrations]
    if len(set(ids)) != len(ids):
        err('页面 id 重复: %s' % ', '.join(sorted(ids)))
    orders = [r[1] for r in registrations]
    if len(set(orders)) != len(orders):
        err('页面 order 重复: %s' % ', '.join(str(o) for o in sorted(orders)))
    homes = [r for r in registrations if r[2] is None]
    if len(homes) != 1 or homes[0][1] != 0:
        err('总览页应唯一且 order=0（它必须是 QStackedWidget 第 0 页）')

    svc = [f for f in os.listdir(abspath('services')) if f.endswith('service.h')]
    if len(svc) != 8:
        warn('services/ 业务服务数 = %d（约定列表为 8 个 + processrunner）' % len(svc))
    for fn in sorted(os.listdir(abspath('services'))):
        if fn.endswith('service.cpp') and 'HMI_REGISTER_SERVICE(' not in read(abspath('services', fn)):
            err('服务未在 ServiceRegistry 自注册: services/%s' % fn)

    # 10) buildroot 三件套 / 部署 / 配置
    must = [
        'buildroot/config/buildroot/allwinner/auto/qt-demo/hmi-t113s4/Config.in',
        'buildroot/config/buildroot/allwinner/auto/qt-demo/hmi-t113s4/hmi-t113s4.mk',
        'buildroot/config/buildroot/allwinner/auto/Config.in.append',
        'notes/hmi-t113s4/sync-src.sh',
        'notes/hmi-t113s4/install-buildroot.sh',
        'deploy/adb-deploy.sh',
        'tools/gen-i18n.sh',
        'etc/hmi.ini',
        'resources/qss/style.qss',
    ]
    for f in must:
        if not os.path.isfile(abspath(*f.split('/'))):
            err('缺少交付文件: %s' % f)
    mk = read(abspath('buildroot/config/buildroot/allwinner/auto/qt-demo/hmi-t113s4/hmi-t113s4.mk'))
    for token in ('generic-package', 'BR2_PACKAGE_HMI_T113S4', 'SITE_METHOD = local',
                  '/usr/bin/QtApplication', 'CONFIG+=hmi_no_lrelease'):
        if token not in mk and token != 'BR2_PACKAGE_HMI_T113S4':
            err('hmi-t113s4.mk 缺少: %s' % token)
    if 'BR2_PACKAGE_HMI_T113S4' not in read(abspath('buildroot/config/buildroot/allwinner/auto/qt-demo/hmi-t113s4/Config.in')):
        err('Config.in 缺少 BR2_PACKAGE_HMI_T113S4')

    # 11) 自定义信号 / 槽引用拼写（Qt5 新语法 connect 最容易写错名字）
    class_members = {}
    signal_names = set()
    for h in all_headers:
        text = read(h)
        cls = re.search(r'\bclass\s+(\w+)\s*:', text)
        if cls:
            class_members[cls.group(1)] = set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', text))
        sig = re.search(r'\bsignals\s*:(.*)', text, re.S)
        if sig:
            signal_names |= set(re.findall(r'\b([A-Za-z_]\w*)\s*\(', sig.group(1)))

    all_cpp = []
    for dirpath, dirnames, filenames in os.walk(ROOT):
        dirnames[:] = [d for d in dirnames if d not in ('.git', 'build')]
        for fn in filenames:
            if fn.endswith('.cpp'):
                all_cpp.append(os.path.join(dirpath, fn))
    for cpp in all_cpp:
        text = read(cpp)
        for cls, member in re.findall(r'&(\w+)::(\w+)', text):
            if cls in class_members and member not in class_members[cls]:
                err('%s 引用了 %s 未声明的成员: %s' % (os.path.relpath(cpp, ROOT), cls, member))
        for name in re.findall(r'\bemit\s+(\w+)\s*\(', text):
            if name not in signal_names:
                err('%s emit 了未声明的信号: %s' % (os.path.relpath(cpp, ROOT), name))

    # 12) 设计模式基础设施：文件齐备 + 关键角色到位
    pattern_files = [
        'core/singleton.h',               # 单例
        'core/appsettings.h',             # 单例 + 外观
        'core/commandspec.h',             # 建造者
        'core/commandrunner.h',           # 策略
        'core/processcommandrunner.h',    # 适配器（QProcess）
        'core/loggingcommandrunner.h',    # 装饰器
        'core/simulatedcommandrunner.h',  # 空对象 / 桩
        'core/command.h',                 # 命令 + 组合
        'core/commandinvoker.h',          # 调用者
        'core/statusbus.h',               # 观察者 + 中介者
        'core/statetable.h',              # 状态机（表驱动）
        'core/playerstate.h',             # 状态（多态）
        'core/pagefactory.h',             # 工厂 + 自注册表
        'core/ideviceservice.h',          # 依赖倒置接口
        'core/serviceregistry.h',         # 服务定位器
        'core/deviceservice.h',           # 模板方法（服务骨架）
        'models/wifiscanmodel.h',         # Model/View
    ]
    for f in pattern_files:
        if not os.path.isfile(abspath(*f.split('/'))):
            err('缺少设计模式基础设施: %s' % f)
    for inc in ('include(core/core.pri)', 'include(models/models.pri)'):
        if inc not in pro_text:
            err('.pro 未包含: %s' % inc)

    mw = read(abspath('mainwindow.cpp'))
    if 'PageFactory' not in mw or 'StatusBus' not in mw:
        err('MainWindow 应通过 PageFactory / StatusBus 解耦具体页面与服务')
    if 'virtual void buildContent' not in read(abspath('pages', 'basepage.h')):
        err('BasePage 缺少 buildContent() 钩子（模板方法）')
    if 'buildContent()' not in read(abspath('pages', 'basepage.cpp')):
        err('BasePage::initialize() 应负责调用 buildContent()')
    if 'CommandSequence' not in read(abspath('services', 'wifiservice.cpp')):
        err('WifiService 应给出 CommandSequence（组合命令）用法')
    if 'makeConnectSequence' not in read(abspath('services', 'ethservice.cpp')):
        err('EthService 应给出 CommandSequence（组合命令）用法')
    if 'PlayerState' not in read(abspath('services', 'mediaservice.cpp')):
        err('MediaService 应把播放动作委托给 PlayerState（状态模式）')
    if 'setState' not in read(abspath('core', 'playerstate.cpp')):
        err('PlayerState 实现应通过 ctx.setState() 迁移状态')

    # 汇总
    print('=' * 64)
    print('HMI-T113S4 工程结构校验')
    print('=' * 64)
    print('SOURCES=%d  HEADERS=%d  RESOURCES=%d  TRANSLATIONS=%d'
          % (len(sections['SOURCES']), len(sections['HEADERS']),
             len(sections['RESOURCES']), len(sections['TRANSLATIONS'])))
    print('导航项=%d  页面头文件=%d  服务头文件=%d' % (nav_count, len(page_files), len(svc)))
    for w in warnings:
        print('[WARN] ' + w)
    for e in errors:
        print('[FAIL] ' + e)
    print('-' * 64)
    if errors:
        print('失败：%d 项错误，%d 条警告' % (len(errors), len(warnings)))
        return 1
    print('通过：%d 条警告' % len(warnings))
    return 0


if __name__ == '__main__':
    sys.exit(main())
