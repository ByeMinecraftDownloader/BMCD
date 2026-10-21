# Windows 运行稳定性评估（截至 0.4.0 / M4）

评估日期 2026-10-21。**方法：源码审计 + 平台依赖清点 + 端点实测**；本机是 Arch Linux，
没有 Windows 环境，所以本文所有结论都来自「代码里有没有平台假设」和「依赖在 Windows 上是否成立」，
不是真机跑出来的。分不清的地方我标了「需实测」。

**结论先说：代码层面可移植性不错（8/10），但分发与 Windows 特有问题没处理（3/10）。**
预计是「能编译、能启动、能拉列表、能下载」，但会撞上 3 个必现问题（控制台窗口 / TLS 后端缺失 /
重命名冲突）和 1 个默认路径错误。这四条不修，Windows 用户第一分钟就会踩到。

---

## 1. 已经确认「没问题」的部分

| 检查项 | 结果 |
|---|---|
| POSIX 专用调用 | **零**。`src/` 里没有 `<unistd.h>`/`<sys/*>`、`chmod`、`fork`、`execv`、`#include <windows>`；全项目没有 `QProcess`（grep 实测） |
| 无头模式 | `src/main.cpp:283-286` 在创建 `QGuiApplication` 之前就用 `isHeadlessInvocation()` 分流到 `QCoreApplication`，Windows 上没有 display 也能跑 `--plan` |
| 平台识别 | `src/core/PlanParser.cpp:105-113` 的 `hostOsName()` 已经是 `#if defined(Q_OS_WIN) → "windows"`；natives 分类键取 `windows-${arch}`，`${arch}` 走 `QSysInfo::currentCpuArchitecture()`，arm64 判定也对 |
| 截图/离屏 | `main.cpp:314-340` 用 `QQuickWindow::grabWindow()`，不依赖 X11；配 `QT_QPA_PLATFORM=offscreen` 即可，与平台无关 |
| 色板缺失 | `src/theme/Palette.cpp:58-62` 读 `QStandardPaths::GenericDataLocation`（Windows 上是 `%LOCALAPPDATA%`）。目录不存在 → `resolveSchemePath()` 返回空 → 走 `applyFallback()`（`Palette.cpp:203-221`），**不会崩**，只是永远深色 |
| 文件监听 | `Palette.cpp:118-130` 先 `QFileInfo::exists()` 再 `addPath`，Windows 上目录/文件不存在只是不监听，不报错 |
| 路径拼接 | 统一用 `/`（`DownloadTask.cpp:37-40`），Qt 在 Windows 上接受 `/`，`QDir`/`QFileInfo` 会归一化 |
| 缓存目录 | `VersionService.cpp:50-56` 用 `QStandardPaths::CacheLocation`，Windows 上是 `%LOCALAPPDATA%\BMCD\bmcd\cache` |
| 窗口拖动/缩放 | `qml/Main.qml:143` `startSystemMove()`、`:682` `startSystemResize(edges)` 在 Qt 6 Windows 后端有实现（原生 move/size loop），无边框窗口能拖能拉 |

---

## 2. P0：会直接卡住用户的问题

### P0-1 双击启动会多出一个黑色控制台窗口

`CMakeLists.txt:13-15` 的 `qt_add_executable(bmcd …)` 没有 `WIN32_EXECUTABLE` 属性，MSVC 默认生成
**控制台子系统**程序。双击 exe → 黑框 + GUI 同时出现；关 GUI 黑框不一定消失。

但直接改成 `WIN32_EXECUTABLE TRUE` 会把另一头弄坏：`--plan`/`--download` 的 `out()` 是 stdout，
GUI 子系统进程**没有**控制台，从 cmd 里调用会看不到任何输出（`MC_LOG` 兜底能救，但那是调试路径）。

建议：

```cmake
if(WIN32)
    set_target_properties(bmcd PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
```

然后在 `runHeadless()` 里（仅 Windows）`AttachConsole(ATTACH_PARENT_PROCESS)`，失败再 `AllocConsole()`，
并把 `stdout` 重定向到 CONOUT$；同时 `SetConsoleOutputCP(CP_UTF8)`（见 P2-4）。
也就是「GUI 子系统 + 无头模式自己接控制台」，这是 Qt 项目在 Windows 上的标准做法。

### P0-2 HTTPS 全靠 OpenSSL 3 运行时，windeployqt 不会带

Qt 6 的 Windows 二进制包**不含** OpenSSL；`libqopensslbackend.so` 换成 `qopensslbackend.dll` 后，
运行时必须能找到 `libssl-3-x64.dll`、`libcrypto-3-x64.dll`（Qt 官方安装器里在
`C:\Qt\Tools\OpenSSL\Win_x64\bin`）。找不到时的表现**不是**「TLS 失败」这么温柔，而是：

```
No TLS backend is available
QSslSocket::connectToHostEncrypted: TLS initialization failed
```

→ 版本清单 0 条、下载 0 个文件、UI 上只显示「无法获取版本清单」。
本项目的 AppImage 阶段已经踩过一次同样的坑（插件路径导致 TLS 后端没加载），症状一模一样。

建议：打包脚本里显式拷贝这两个 DLL 到 exe 同级；`--version` 之类不涉网的路径不受影响，
所以「装完能开窗、点不动列表」这种半死状态很难被用户自己诊断 → 在「无法获取版本清单」的错误文案里
加一句「请检查 OpenSSL 运行库是否完整」会省很多支持成本。

### P0-3 `QFile::rename` 覆盖失败 = 下载直接失败（Windows 独有）

`DownloadTask.cpp` 的落地流程：

```
:164  QFile::remove(absPath);                       // 返回值被忽略
:177  open(.part, ReadWrite|Append)
:321  QFile::rename(.part → absPath)                // 失败 → failOne("无法重命名")
```

POSIX 上 `rename()` 会原子覆盖同名文件；**Windows 上目标文件存在、被占用（正在运行的 Minecraft
读着这个 jar、Defender 正在扫、OneDrive 正在同步）时 `MoveFile` 会失败**。更糟的是 `:164` 的
`remove()` 失败被静默吞掉，于是 `.part` 下载完成、校验通过，最后一步重命名炸掉，用户看到
「无法重命名 mods/xxx.jar」。

建议：

1. `:164` 改成 `if (QFileInfo::exists(absPath) && !QFile::remove(absPath)) { failOne("文件被占用"); return; }`
   —— 早失败，错误信息才准确。
2. `:321` 失败后重试 3 次（100/300/900 ms 退避），仍失败再报错，日志里带上「请关闭 Minecraft 后重试」。
3. 若要做到真正原子替换，Windows 上用 `ReplaceFileW`（Qt 没有封装，需要 `#ifdef Q_OS_WIN` 一小段）。

---

## 3. P1：大概率会遇到的问题

### P1-1 默认目标目录在 Windows 上是错的

`src/main.cpp` 的 `resolveTarget()`：

```cpp
return parser.isSet("target") ? parser.value("target")
                              : QDir::homePath() + QStringLiteral("/.minecraft");
```

Windows 上 Minecraft 官方目录是 `%APPDATA%\.minecraft`（`C:\Users\<name>\AppData\Roaming\.minecraft`），
`QDir::homePath()` 给的是 `C:\Users\<name>`。结果：下载全部落到 `C:\Users\<name>\.minecraft`，
游戏根本看不到，用户会以为「下载器坏了」。**这是 Windows 上最容易误伤人的一条**。

建议：

```cpp
#if defined(Q_OS_WIN)
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
#else
    const QString base = QDir::homePath();
#endif
    return base + QStringLiteral("/.minecraft");
```

（`GenericConfigLocation` 在 Windows 上就是 `%APPDATA%`。CLI 的 `--target` 仍可覆盖。）

### P1-2 长路径（>260 字符）

资产对象路径很短（`assets/objects/ab/<40 hex>`），但整合包 `overrides/` 可能很深，加上用户把实例放在
`C:\Users\<中文名>\Documents\Minecraft\…`，很容易越过 260。Qt 6 的 `QFile`/`QDir` 在 Windows 上
**不会**自动给路径加 `\\?\` 长路径前缀（只有显式传入带前缀的路径才有效），表现是
`open()` 直接返回 false，日志里只有「无法写入 xxx」。

建议：封装一个 `nativePath()`（`#ifdef Q_OS_WIN`，`toNativeSeparators` + 长度 > 240 时加 `\\?\`），
所有 `QFile`/`QDir` 入口统一走它；文档里同时建议用户把实例放在浅目录。
（需实测：Qt 6.12 是否已部分处理，我这份环境验证不了。）

### P1-3 完全不读系统代理

grep 实测：`src/` 里没有任何 `QNetworkProxy`。Qt 的 `QNetworkAccessManager` **默认不用系统代理**。
Windows 用户开了 Clash/公司代理、或走 PAC 时，模型下载能通、BMCD 一律超时，
而且报错是「无法获取版本清单」，看起来像「网络不好」。

建议：初始化时（`runHeadless()` 与 GUI 分支各一次）调用
`QNetworkProxyFactory::setUseSystemConfiguration(true)`，Windows 上会读 WinINet 设置（含 PAC）。

### P1-4 没有磁盘空间预检

`QStorageInfo` 全项目没用过。完整版 26.3 是 610 MB，整合包 1–2 GB，写满盘后每文件都失败，
最终只报「N 个文件下载失败」，用户不知道为什么。

建议：`runDownload()` 开跑前 `QStorageInfo(target)` 比 `bytesAvailable()` 与 `plan.totalBytes * 1.1`，
不足就提前 `error: 磁盘剩余空间不足（需要 X，可用 Y）`。

### P1-5 杀软/同步盘会让 5000+ 小文件的下载变慢且偶发失败

`assets/objects` 是 5000 个几十 KB 的小文件，Defender 实时保护对「写文件 + 改名」的每一次都扫，
典型表现是速度在 1 MB/s 上下、偶发 `move` 失败（与 P0-3 叠加）。这不是代码 bug，但建议：
保持并发 6（别再提高）、失败重试（现有 3 次够用）、README 里给一句「可把实例目录加入 Defender 排除项」。

---

## 4. P2：体验与分发

| # | 问题 | 位置 / 证据 | 建议 |
|---|---|---|---|
| P2-1 | 窗口**只有关闭按钮**，没有最小化/最大化，标题栏双击也不会最大化（无边框导致失去 Windows Snap Layouts） | `qml/Main.qml` 全文只找到 `:183 onClicked: root.close()`；`flags: Qt.Window \| Qt.FramelessWindowHint`（`:16`） | Windows 上补 min/max 按钮 + 标题栏 `onDoubleClicked`；或 Windows 分支改用系统标题栏（`Qt.Window` 不带 Frameless），代价是配色不再统一 |
| P2-2 | 主题永远是内置深色，不跟系统浅色/深色 | `Palette.cpp:58-62`（只找 XDG 的 color-schemes）+ `:203-221`（fallback） | 用 `QGuiApplication::styleHints()->colorScheme()`（Qt 6.5+，Windows 11 会正确返回）切换浅色基线；深色模式标题栏 Qt 6.5+ 自动处理 |
| P2-3 | `font.family: "monospace"` 在 Windows 不是合法字体族名 | `qml/Main.qml:468` | 改成平台三元式：Windows → `"Cascadia Mono"`/`"Consolas"`，Linux → `"monospace"` |
| P2-4 | 中文日志在 cmd 里可能乱码（GBK 代码页 vs UTF-8 字节） | `main.cpp:198-217` 的 `installFileLog()` 与 `out()` | `SetConsoleOutputCP(CP_UTF8)`（配合 P0-1 的 CONOUT 重定向） |
| P2-5 | 没有 `.rc` 图标/版本资源、没有签名 | `CMakeLists.txt` 无 `.rc`；打包只有 AppImage/Flatpak | 加 `app.rc`（`IDI_ICON1 ICON "bmcd.ico"` + VERSIONINFO）；发布前签名，否则 SmartScreen 首次运行会拦 |
| P2-6 | 没有 Windows 打包脚本 | `CMakeLists.txt:64-85` 的 install 规则是 Linux 布局（`share/applications`、`share/icons`） | 加 `cmake --install` + `windeployqt --qmldir qml` + `windeployqt --qml-deploy-dir`；产物建议「zip 绿色版」先起步，再考虑 NSIS/Inno |
| P2-7 | UA 仍写 `BMCD/0.3`（版本 0.4.0） | `DownloadTask.cpp:197` | 用 `QCoreApplication::applicationVersion()` 拼，避免再次漂移 |

---

## 5. 构建与冒烟矩阵（Windows 上要跑一遍）

```powershell
# 依赖：VS 2022 Build Tools + Qt 6.12 (msvc2022_64) + OpenSSL 3
cmake -S . -B build-win -G "Visual Studio 17 2022" -A x64 ^
      -DCMAKE_PREFIX_PATH="C:\Qt\6.12.0\msvc2022_64"
cmake --build build-win --config Release
& .\build-win\Release\bmcd.exe --plan 26.3      # 期望 java 25 / 5216 / 610373334
& .\build-win\Release\bmcd.exe --download 26.3 --target build-win\mc --limit 12
& .\build-win\Release\bmcd.exe --download 26.3 --target build-win\mc --limit 12   # 期望 done: 0 downloaded, 12 already present
```

冒烟项（按优先级）：

1. `--plan 26.3` 数字与 Linux 一致（验证 TCP/TLS/JSON 链路）。
2. `--download` 两次，第二次 `0 downloaded`（验证 sha1 + 已存在跳过）。
3. 下载中途 `Ctrl+C` 后重跑（验证 `.part` 续传）；**再开着 `mc.exe` 占住同名文件重跑**（验证 P0-3）。
4. 用户名带中文/空格、实例路径超过 200 字符（验证 P1-1/P1-2）。
5. 系统代理开启（Clash 规则模式）下刷新列表（验证 P1-3）。
6. 100% / 150% / 200% 缩放、双屏不同 DPI 拖动窗口（验证无边框窗口缩放）。
7. 浅色模式（验证 P2-2 是否刺眼）。

---

## 6. 修复顺序与工作量（我的建议）

| 顺序 | 事项 | 预估 |
|---|---|---|
| 1 | P1-1 默认目录改成 `%APPDATA%\.minecraft`（一行 `#if`，收益最大） | 10 分钟 |
| 2 | P0-1 控制台/子系统 + `AttachConsole` | 1–2 小时 |
| 3 | P0-3 重命名失败早退 + 重试 | 1 小时 |
| 4 | P0-2 打包时拷 OpenSSL DLL + 错误文案 | 1 小时（含 windeployqt 脚本） |
| 5 | P1-3 系统代理、P1-4 磁盘预检 | 1 小时 |
| 6 | P2-6 windeployqt + zip 绿色包 + 冒烟脚本 | 半天 |
| 7 | P2-1 窗口按钮、P2-2 主题、P2-3 字体 | 半天 |

**总判断**：核心下载逻辑（清单 → 计划 → 分块下载 → sha1 → 落盘）是纯 Qt Core/Network，
在 Windows 上成立；风险集中在「Windows 的默认路径、文件锁、TLS 运行时、分发」这四件事上，
都不是架构问题，是补丁问题。修完 P0 + P1-1 之后，我预计 Windows 上「开箱可用」的概率很高。

**没评估到的**：MSVC 编译告警、`qt_add_qml_module` 在 MSVC 下的 `OUTPUT_DIRECTORY` 处理、
ARM64 原生编译、Windows 7/8（不在 Qt 6.5+ 支持范围内，直接排除）。
