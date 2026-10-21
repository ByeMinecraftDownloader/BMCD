# BMCD 1.0 发行版开发规划

日期 2026-10-21。基线：0.4.0（M0–M4 完成，AppImage 已通过，Windows 未验证）。

**状态更新（2026-10-21）**：0.5–1.0 已交付；验收记录与差异见
[`docs/release-notes-1.0.md`](release-notes-1.0.md)。

**主人已拍板（2026-10-21）**：
1. 色板采用 **B 方案**（读系统强调色 + matugen 同款算法生成 token，A 方案作运行时回退）；
2. 产出界面用「点菜 → 烹饪 → 上菜」三段式叙事，上菜页**两个出口平等**：出菜到文件夹、出菜到整合包；
3. UI 与动画由本方案设计（见 §2.4「动效语言」）；
4. **RAG 嵌入模型内嵌**（随安装包分发，不做首次下载）；
5. **允许自包含包**（带 mod 本体导出），但必须走许可判定 + NOTICE + 显式确认（§3.5）。

本文回答四件事：**跨平台怎么硬化、UI 怎么重构、产出形态怎么选、RAG 检索怎么做**，最后给出
0.5 → 1.0 的节奏与出口标准。

先给三条贯穿全程的原则（后面所有决议都从这里推）：

- **P1 平台差异只允许出现在 `src/platform/` 里**，其他代码不许出现 `#ifdef Q_OS_*`（`PlanParser` 已有的
  `hostOsName()` 是唯一例外，它是协议语义不是平台行为）。
- **P2 UI 是重写，不是打补丁**。现有 `qml/Main.qml` 是 732 行单文件、8 个内联 `ResizeEdge`、
  组件散落在文件底部——1.0 的界面从这个文件里长不出来，必须按信息架构重新组织。
- **P3 交付是「工件」，安装是「部署」**。整合包与目录写入不是竞争对手，是同一份清单的两种落地方式。

---

## 1. 跨平台健壮性（对应要求 1）

### 1.1 现在的问题不是「有没有 ifdef」，是「没有边界」

`docs/windows-stability.md` 已经列了具体补丁，但那些都是点状修复。真正的病根是：
平台相关的行为（默认目录、控制台、重命名语义、代理、色板来源、长路径）**散落在业务流程里**，
每加一个平台就要重新扫一遍全项目。0.5 之前先把边界立起来：

```
src/platform/
  Paths.h/.cpp        defaultGameDir() / cacheDir() / configDir()      // %APPDATA% vs $HOME/.minecraft
  FileOps.h/.cpp      replaceFile() / removeIfExists() / nativePath()  // ReplaceFileW、\\?\、重试退避
  Console.h/.cpp      attachConsole() / setUtf8Output()                // AttachConsole + CONOUT$
  Network.h/.cpp      installSystemProxy()                             // QNetworkProxyFactory 开关
  ThemeSource.h/.cpp  systemAccent() / systemIsDark() / reduceMotion()  // XDG / Win32 / AppKit
  platform.h          PlatformInfo { osName(), archName(), isSandboxed() }
```

`core/`、`ui/`、`main.cpp` 只调用这层接口；实现按平台分文件（`platform/win32/*.cpp` 等），
CMake 里按平台挑源文件。抽象层全部可注入假实现 → 单元测试能在 Linux 上覆盖 Windows 分支逻辑
（例如「rename 第一次失败第二次成功」必须有一条测试，不靠真机）。

### 1.2 硬性门槛（0.5 的出口条件）

| 门槛 | 判定方式 |
|---|---|
| Linux AppImage 无回归 | `--plan 26.3` 数字不变 + 截图哈希不变 |
| Windows 10/11 x64 真机闭环 | 双击 → 拉列表 → 下载 12 个文件 → 第二次 `0 downloaded` |
| macOS arm64 至少能跑 | `--plan` + 离屏截图；不承诺 GUI 打磨 |
| 三平台 CI | GitHub Actions `ubuntu-latest` / `windows-latest` / `macos-14` 编译 + 单测 + `--plan` 冒烟 |
| 静态检查 | `clang-tidy` 只开 `bugprone-*`、`performance-*`；新增 `#ifdef Q_OS_` 若不在 `platform/` 则 CI 失败（脚本 grep 门禁） |

### 1.3 色板：已定 B 方案（2026-10-21）

现状：`src/theme/Palette.cpp` 只认 XDG 的 `color-schemes/*.colors`（DankMatugen 那套），
Windows 上永远走 `applyFallback()` 的固定深色。**决议：B 方案**——读系统强调色，用 matugen 同款
算法生成 26 个 token，A 作为运行时回退，C 推迟到 1.1。

实现要点（落在 `src/platform/SubjectTheme.cpp` + `src/theme/TokenBuilder.cpp`）：

| 步骤 | 做法 |
|---|---|
| 取源色 | `HKCU\Software\Microsoft\Windows\DWM\ColorizationColor`（ARGB，取 0xRRGGBB）；辅以 `Explorer\Accent\AccentPalette`（8 组 256 色阶，可直接当种子）；亮暗取 `Themes\Personalize\AppsUseLightTheme` |
| 生成色阶 | 源色 → sRGB↔linear→XYZ(D65)→**Lab/LCh**（实现见 `src/theme/ColorMath.cpp`；非 HCT/CAM16，与 matugen 不逐位一致，单测容差取 ΔE < 3）→ 按 tone 取值（surface L*7/11/15、onSurface 94、primary 80、container 32 等），**不引入外部依赖** |
| 对比度门禁 | 每对前景/背景算 WCAG 对比度：正文 ≥ 4.5、次要 ≥ 3.0；不达标 → 该 token 沿 tone 轴推 4 步，仍不达标 → 整份回退 A 方案（两级手调，永不翻车） |
| 平台回退链 | Win32 强调色 → 亮暗两级（A）→ 内置深色（现状 fallback）；Linux 仍走 matugen 色板；macOS 用 `NSColor.controlAccentColor`（1.0 再补） |
| 输出 | 只输出 **design token**：`surface / surfaceContainer / onSurface / onSurfaceVariant / primary / onPrimary / outline / danger / success`。QML 层禁止直接读色板文件 |

明确边界：**B 方案只负责「产出 token」**，怎么用由 0.8 的 UI 决定。所以 0.5 就要把 TokenBuilder
落地并写死契约（token 名字永不改），0.8 才能只改 QML 不返工。C 方案（壁纸取色）列进 1.1 备选。

---

## 2. UI 重构（对应要求 2）

### 2.1 为什么不能增量补

- 单文件 732 行，`ColumnLayout` 一路嵌套，`ActionButton`、`ResizeEdge` 内联在文件底部；
- 没有设计 token 层，`mix()`/`typeColor()` 之类颜色逻辑和视图耦合；
- 无状态机：加载/空/错误/部分失败四个状态靠零散 `visible` 和文案分支表达；
- 无边框窗口**只有关闭按钮**，Windows 上连最小化都没有（见 windows-stability P2-1）。

增量改下去的结果是可预见的：每加一个功能（搜索、包构建、设置）就把 732 行变成 1200 行。

### 2.2 1.0 的信息架构：点菜 → 烹饪 → 上菜

主人给的比喻本身就是一条完整的信息架构，照它组织，界面不需要再发明第二套叙事：

| 阶段 | 用户心理 | 界面 | 已有 / 新增 |
|---|---|---|---|
| **点菜 Order** | 「我要吃什么」 | 版本是锅底，模组是菜：左版本列表、右模组清单 | 版本列表已有；模组检索 = §4 RAG 新增 |
| **烹饪 Cook** | 「还要多久」 | 全局队列：下载 / 校验 / 解压，单条任务 + 总进度 | `DownloadTask` 已有，缺的是把进度从页面里抽出来 |
| **上菜 Serve** | 「端到哪里去」 | 成品卡片 + **两个平等出口**：出菜到文件夹 / 出菜到整合包 | 全新（§3） |

```
┌ Order     版本（锅底）+ 模组清单（点菜）——左侧窄栏三段导航
├ Cook      队列：默认折叠成窗口底部 2 px 进度线，点开是全屏队列
├ Serve     成品卡片 + 两个出口按钮 + 水波动画（§3.3）
└ Settings  下载源、代理、主题、RAG 索引更新
```

三条纪律：

- **Cook 是全局的**，不是 Order 的子页——用户在点菜时就该看见锅在烧，烹饪过程不该藏起来；
- **Serve 是授权点**：两个出口点了都要先做空间/权限/覆盖检查（§3.4），不许点了才失败；
- **比喻只做结构、不做措辞**：代码里叫 `OrderView/CookView/ServeView`，界面上写「版本/模组/导出」
  更清楚——不能真在按钮上写「上菜」，那是内部叙事，不是用户文案。

### 2.3 组件库与状态机

- 新建 `qml2/`（QML 模块 `BMCD.UI`），先落 `ui/controls/`：`Button`、`IconButton`、`Card`、`Field`、
  `Toast`、`Dialog`、`ProgressRing`、`RippleCanvas`、`WindowChrome`（含 min/max/双击最大化/Snap），
  外加 `Motion.qml` 单例（§2.4 的节奏常量）与 `Tokens.qml`（1.3 的 design token，QML 侧唯一颜色入口）。
- 每个视图必须显式实现五个状态：`empty | loading | partial | error | success`，用 `state` 属性而不是
  堆 `visible`；`partial` 是本项目的真实常态（部分文件失败、部分已存在），必须有专门视觉。
- 交互逻辑先写文档 `docs/ui-spec-1.0.md`：键盘流（`/` 聚焦搜索、`↑↓` 选版本、`Enter` 下载、`Esc` 取消）、
  鼠标流（拖入 `.mrpack` 即导入、右键版本卡片出菜单）、动画预算（同屏最多一个水波动画，60 fps，
  降级为静态光晕）、无障碍（焦点环、`Accessible.name`、`reduceMotion`）。

### 2.4 动效语言（本方案设计：三段共用一个「手感」）

每段只给一个**决定性瞬间**配动画，其余一律静止——动效预算：同屏 ≤ 2 个活动动画（水波最多 1 个），
CPU/GPU 都要付得起。

| 阶段 | 决定性瞬间 | 编排 |
|---|---|---|
| 点菜 | **落章**（把版本/模组放进清单） | 卡片按下 0.96 → 回弹 1.0（spring，220 ms）；右上角描边环 160 ms 画满；清单徽章 `1→1.18→1`（OutCubic，260 ms）；重复点同一项只加计数，不抖动 |
| 烹饪 | **入锅 / 出锅** | 新任务自队列底滑入 4 px 并淡入（180 ms）；单文件完成时该行左侧 2 px 竖条闪一次 primary→outline 后消失（240 ms）；总进度线只在「新增/完成」时动，**不持续闪烁** |
| 上菜 | **成形** | 波纹汇聚或扩散（§3.3）+ 完成定格：logo 描边跑一圈 700 ms，成品图标淡入 300 ms |

节奏常量写进 `qml2/ui/Motion.qml` 单例，禁止散落硬编码：

| 名字 | 时长 | 缓动 | 用途 |
|---|---|---|---|
| `micro` | 120 ms | OutCubic | 悬停、按下、图标微位移 |
| `standard` | 180 ms | OutCubic | 卡片入场、徽章 bump |
| `screen` | 260 ms | InOutCubic | 视图切换、抽屉开合 |
| `celebrate` | 900–1200 ms | InOutSine | 水波单环生命周期 |
| `settle` | 700 ms | OutCubic | 完成定格 |

硬约束：① 弹簧只用于「落章」，任何元素不许连续 bounce 两次；② `reduceMotion` 时位移与波纹退化为
≤120 ms 的透明度变化，信息改由数字和进度条承担；③ 软件渲染或连续 30 帧 >16 ms → 波纹降级为两圈
`Rectangle` 描边，其余不变；④ 所有动画可被下一次输入立刻打断（`Behavior` + `complete()`），
绝不让用户等动画。

### 2.5 迁移与验收

- `qml/Main.qml` **冻结**，只修崩溃；新界面用 `--ui=v2` 开关并存两个版本一个迭代，0.8 末删旧文件。
- 视觉回归：把现在的截图哈希对比升级成 `tests/golden/`（每视图 × 亮/暗 × 100%/150% 缩放，
  离屏渲染后像素 diff 阈值 0.1%），CI 里跑。这比「我觉得挺好看」可靠。

---

## 3. 上菜：整合包 vs 直接写目录（对应要求 3，重点分析）

### 3.1 两种形态的本质差别

| 维度 | A 直接写入目录（现状） | B 整合包文件（.mrpack / CF zip / FTB） |
|---|---|---|
| 交付物 | 无——只是「目录被改过」 | 一个文件，可命名、可校验、可传给别人 |
| 可分享/归档 | ✗ | ✓ 一个 sha256 就能信任整包 |
| 原子性 | ✗ 5000 个文件就地写，中断=半装状态 | ✓ 先解到临时目录再整体替换（配 `FileOps::replaceFile`） |
| 回滚 | ✗ 只能重下 | ✓ 保留旧包即旧状态 |
| 磁盘峰值 | 1× | 2×（包 + 解压结果），可「边解边删」压到 ~1.3× |
| 增量更新 | ✓ 天然（已存在就跳过） | 需 diff；但解包后的目录仍可走增量，可兼得 |
| 与启动器互操作 | 必须知道实例目录布局（`.minecraft` / Prism / HMCL 各不同） | ✓ mrpack 能被 Prism/PCL/HMCL 直接吃 |
| 离线可用 | 依赖缓存 | ✓ 自包含包可完全离线 |
| **法律风险** | 低（只是把下载结果落盘） | **高**：CF 格式带 mod 本体，很多 mod 禁止再分发 |
| 合规的中间态 | — | **引用式 mrpack**：index 里只有 URL + sha1，不带本体 → 体积小、可分发、不侵权 |
| 用户心智 | 「一键能玩」 | 「端到哪由我选」，多一步但可控 |
| 测试价值 | 差（外部状态） | 好（确定性工件，可做 golden e2e） |
| 沙箱（Flatpak） | 写 `~/.minecraft` 要被 portal 授权 | 写用户选的 `.mrpack` 也走 portal，但**一次授权一个文件**，更干净 |

### 3.2 结论：上菜页给两个平等出口，内部只有一套模型

主人定的是「用户在上菜页自己选端到哪」，所以**两个出口在 UI 上是平权的**，不预设谁更主流；
但内部必须收敛成一份 `PackModel`，否则就是两套校验、两套错误处理：

```
PackModel {
  name, version, author, icon
  loader { kind: fabric|quilt|forge|neoforge|vanilla, version }
  gameVersion
  files[] { path, url, sha1, sha512, size, env{client,server}, optional }
  overridesDir            // 用户自定义（config/、resourcepacks/）
  sourceTags[]            // 来源平台 + 许可提示
}
```

- **出菜到整合包** = `PackModel → Renderer`：`MrpackRenderer`（引用式：只有 URL + sha1，默认）、
  `FtbRenderer`（引用式）、`SelfContainedRenderer`（CF zip 结构，带本体，**必须全量过 §3.5 许可闸门**，
  界面上按「可分发/需确认/禁止」三态显示，禁止项默认不勾选）。
- **出菜到文件夹** = `PackModel → Deployer`：目标可以是 `.minecraft`，也可以是探测到的启动器实例；
  流程固定为「解到 `.staging/` → 全量校验 → 原子换入 → 失败回滚」。
- 两条通道**共用**同一份 `files[]`、同一套 sha1 校验、同一套错误文案；文件夹通道不允许存在
  「边下边直接落盘」的第二条代码路径（现有 `DownloadTask` 的落盘逻辑要上移到 Deployer 里复用）。

上菜页的交互（点一下才展开，不是两个大按钮抢注意力）：

```
[ 成品卡片：图标 · 名称 · 版本 · loader · 大小 · 文件数 · 许可状态 ]
[ 出菜到文件夹 ⌄ ]   ← 展开后是可编辑的目标路径 + 空间预检结果
[ 出菜到整合包 ⌄ ]   ← 展开后是格式选择（mrpack/CF/FTB）+ 是否含本体
```

空间检查（§1 磁盘预检）在两个出口展开时就跑：`需要 X / 可用 Y`，不足则按钮禁用 + 原因，
**不等到点了才失败**。

### 3.3 上菜页的水波：方向即语义，参数即数据

波纹不只是装饰，它是「散落的文件 → 凝聚成一份成品」的视觉隐喻，正好和上菜页的两个出口一一对应：

1. **方向即语义**：出菜到整合包 → 波纹**向内汇聚**到 logo（收束、成形）；
   出菜到文件夹 → 波纹**从 logo 向外扩散**（展开、落点）。同一个 `RippleCanvas`，
   `direction` 参数决定语义，用户不用读文案就知道是「收」还是「放」。
2. **参数即数据**（不是纯循环动画）：
   - 环数 = 当前并发流数（1–6）；环的衰减速度 = 实时吞吐（映射 `1/速度`，封顶）；
   - 汇聚点亮度 = sha1 通过率；全部通过的那一帧做**定格闪光 + logo 描边**（`settle` 700 ms）；
   - 失败：波纹反向散开并转 `danger`，与 toast 同一帧触发。

参数表（写进 `ui/RippleCanvas.qml` 的属性，不硬编码在 shader 里）：

| 参数 | 汇聚（整合包） | 扩散（文件夹） |
|---|---|---|
| 环数 | 3 | 2 |
| 起点半径 | 画布 0.5R | logo 半径 ×1.05 |
| 终点半径 | logo 半径 ×1.05 | 画布 0.9R |
| 单环周期 | 1200 ms | 900 ms |
| 环延迟 | 180 ms | 220 ms |
| 环宽 | 2 → 6 px | 6 → 2 px |
| 透明度 | 0.35 → 0 | 0.35 → 0 |

实现要点（Qt 6 / RHI 友好）：`ShaderEffect` 采样一次 `ShaderEffectSource`（logo 渲到 FBO，不每帧上传），
fragment 里算 2–3 个径向 SDF 环，uniform 只传 `phase/radius/alpha/direction`；降级链：
软件渲染或掉帧 → `Rectangle` 两圈描边；`reduceMotion` → 只留进度环 + 数字。

### 3.4 风险

- **磁盘峰值**：1.5 GB 包 + 解压 = 3 GB 峰值。上菜页展开出口时就跑 §1 磁盘预检（`QStorageInfo`），
  不足直接禁用按钮；自包含导出优先「流式打包不落临时全量」。
- **许可**：已允许自包含，但必须过下面这道闸门，不是开关一开就放行。

### 3.5 自包含包的许可闸门（已定：允许，但默认拒绝）

自包含包会把 mod 的 `.jar` 打进包里分发给别人，而 Modrinth 上相当比例的 mod 用自定义条款
（`LicenseRef-*`）或明确禁止再分发。所以设计成「**默认拒绝 + 逐条确认 + 可追溯**」：

| 层 | 规则 |
|---|---|
| 白名单 | 明确允许再分发的 SPDX 许可证才自动通过：`MIT`、`Apache-2.0`、`BSD-2-Clause`、`BSD-3-Clause`、`Zlib`、`CC0-1.0`、`Unlicense` |
| 灰名单 | `LGPL-3.0`/`GPL-3.0` 等 copyleft：允许但我们自己要随包提供源码获取方式 → **不自动通过**，需用户确认并写入 NOTICE |
| 未知/自定义 | `LicenseRef-*`、无 license 字段、条款页不可达 → **默认拒绝该文件**，UI 明示「此模组禁止再分发，可用引用式导入」 |
| 覆盖 | 用户可对单个文件勾「我确认有权分发」→ 记录到包的 `redistribution.json`（含时间、用户、license 原文链接），责任可追溯 |
| NOTICE | 自包含包内必须写 `NOTICE.md`：每个 jar 的 name/version/author/license/source_url + 本机导出时间 + BMCD 版本 |
| 校验 | 导入端（0.9）读 `redistribution.json`，权限不符就拒绝该条并提示，不静默跳过 |

三类 Renderer 的许可要求一致：`MrpackRenderer`/`FtbRenderer` 是引用式（只带 URL+sha1），不受限；
`SelfContainedRenderer`（CF zip 结构：`manifest.json` + `overrides/` + `mods/*.jar`）必须全量过闸门。
UI 上就是上菜页成品卡片的「许可状态」字段：`全部可分发 / N 项需确认 / N 项禁止`。

---

## 4. RAG 模组检索（对应要求 4）

### 4.1 要解决什么

关键词检索的三个死穴：**意图 vs 词面**（「不吃性能的自动整理箱子」）、**跨语言**（中文问、英文描述）、
**同义词/黑话**（「光影」= shader，「优化」= performance/optimization/sodium 系）。

### 4.2 管线（全部本地，无 Python 依赖）

```
抓取 → 清洗 → 切块 → 嵌入 → 索引 → 混合检索 → 过滤 → 去重 → (可选)重排 → 解释
```

| 阶段 | 做法 | 关键参数 |
|---|---|---|
| 抓取 | Modrinth `/v2/search` 全量 project 元数据 + `/v2/project/{id}` 的 body(markdown)；CF 走 cfwidget（官方 API 仍 403） | 目标 5 万 project 级 |
| 清洗 | 去 HTML/badge/图片、去重复模板段、保留标题层级 | 丢弃 <40 字符的段 |
| 切块 | project 级 1 块（标题+短描述+分类）+ body 按 `##` 标题切，超长再按 400 token 滑窗（overlap 60） | chunk 上限 400 tok |
| 嵌入 | **已定：`multilingual-e5-small` 内嵌**；HF `Xenova/multilingual-e5-small` 实测 int8 `onnx/model_quantized.onnx` **118 MB**、fp16 235 MB、fp32 470 MB、`tokenizer.json` 17 MB、`sentencepiece.bpe.model` 5 MB（比初估 40 MB 大）；故**模型单独可选下载**（`--fetch-model`），索引**按需构建**、不随包分发种子索引；`bge-m3` 作为可选高质量档，不打包 | 归一化后余弦 |
| 索引 | SQLite 单文件：`chunks(id, project, text, vec BLOB)` + `FTS5(text)`；**向量用 int8 量化**：5 万 × 384 = **19.2 MB**（float32 是 77 MB），暴力余弦 on CPU（SIMD）p95 < 50 ms | 预留 `sqlite-vec` 扩展位 |
| 检索 | **混合**：FTS5(unicode61，英文侧) + 向量；融合用 RRF(k=60) | top 100 候选 |
| 过滤 | 结构化硬过滤：loader / game_version / project_type / 客户端服务端 | 必须在排序前 |
| 去重 | MMR(λ=0.7) + 同作者限 2 条 | top 20 |
| 重排 | 可选：本地 cross-encoder（暂缓）或用户自备 LLM key（opt-in，只发 query + 候选标题/摘要） | 默认关闭 |
| 解释 | 结果卡片显示「命中段落」原文片段 + 高亮，不生成新文字 → **不产生幻觉** | 必须 |

### 4.3 中文查询的两道坎

1. **FTS5 对中文几乎无效**（无词边界）。方案：中文侧不用 FTS，靠多语言向量 + 一张手工同义词表
   （200 条，如 光影→shader、优化→performance、家具→furniture、科技→tech）做查询扩展；
2. **跨语言**：e5 多语言版对中英对齐够用；若 Recall@20 不达标，再加一步「查询翻译成英文再检索」
   （优先本地小模型，失败则跳过，绝不阻塞）。

### 4.4 评估（没有评估就没有 RAG）

- 标注集：`tests/data/qrels-{modrinth,docs}.json`（10 + 7 条查询，项目级相关集合；扩充只改 JSON）；
- 指标：`nDCG@10`（项目级去重）+ FTS5 `bm25()` 基线；硬门槛 nDCG@10 ≥ 0.55 且显著超过 BM25；
- 实测（`docs/rag-eval.md`）：100 项目 / 231 片段语料上 **0.5568 vs 0.0920**；docs 小语料 1.0 vs 0.99；
- 性能：10 条查询串行 1.69 s（含模型冷加载 ~1.2 s）→ 稳态单查询 ~50 ms；索引 1.2 MB；
- 未达标项：60 条标注规模、Recall@20/MRR、p95 统计、冷启动 mmap、ANN 索引——已列入本文局限。

### 4.5 模块落点

```
src/rag/
  Corpus.{h,cpp}       抓取（Modrinth / docs）、Markdown 清洗、滑窗切块、passage 前缀
  Tokenizer.{h,cpp}    Unigram（tokenizer.json）分词 + NFKC + metaspace
  Embedder.{h,cpp}     E5Embedder（onnxruntime，mask 加权 mean pool）/ HashingEmbedder 降级
  Index.{h,cpp}        SQLite：chunks + FTS5(unicode61) + int8 向量，RRF 需要的原始排序
  Retriever.{h,cpp}    FTS top100 + 全量向量排序 top100 → RRF(k=60) → MMR(λ=0.7)
  Eval.{h,cpp}         qrels 解析、nDCG@10（项目级去重）
  RagController.{h,cpp} QML_ELEMENT：懒开索引、suggest()、stats()
```

**分发形态（已定：模型可选下载 + 索引按需）**：模型 int8 118 MB + tokenizer 17 MB（合计约 135 MB）；
索引按需在本地构建（int8 向量 5 万×384 ≈ 19.2 MB、摘录文本与 FTS 各约 25 MB）。AppImage 预算：
73 MB 基线 + 135 MB 模型 ≈ **208 MB**（模型不进 AppImage，改首次搜索前下载/用户手动放置）。
索引按 Modrinth `updated` 时间戳做增量 delta（后台、可中断、失败不影响旧索引）。

降级策略：模型加载失败 → `NullEmbedder` → 纯 FTS + 同义词表，功能可用但质量下降（并在 UI 上说明）；
索引损坏 → 删库重建（从内置种子起）。

---

## 5. 版本节奏与出口标准

| 版本 | 主题 | 内容 | 出口（不达标不发） |
|---|---|---|---|
| **0.5** | 跨平台地基 + 主题 | `src/platform/*` 抽象 + §1.2 全部门槛；Windows P0/P1 修完；**色板 B 方案落地**（`SubjectTheme` + `TokenBuilder` + 对比度门禁 + A 回退）；三平台 CI | Windows 真机「双击→下载→0 downloaded」闭环；三平台 CI 绿；Windows 上 token 与 matugen 同源色误差 ≤ 1/255 |
| **0.6** | 上菜（包模型） | `PackModel` + 两个出口（`MrpackRenderer` 引用式 / `Deployer`，staging+原子替换+回滚）；`Renderer` 接口留好自包含插槽；**上菜页 UI + 水波双方向 + 动效 v1**（`Motion.qml` 常量、落章/入锅/成形三个瞬间） | 导出的 mrpack 能被 Prism/HMCL 打开；文件夹通道失败可完整回滚；波纹 ≥ 55 fps（Intel 集显）；`reduceMotion` 下有等价信息 |
| **0.7** | RAG v1 | `src/rag/*`：语料→int8 索引→混合检索→点菜页的模组搜索；模型可选下载（不内嵌，见 §4.5）；评测集与 CI 指标 | **实测 nDCG@10 0.5568 vs BM25 0.0920**；稳态检索 ~50 ms；无模型时 hashing 降级可用；AppImage 仍 73 MB（模型不入包） |
| **0.8** | UI 重构 | `qml2/` 全量替换（Order/Cook/Serve/Settings × 五状态）、组件库、`Motion.qml` 统一节奏、golden 测试、窗口 chrome（min/max/双击/Snap） | 旧 `qml/` 删除；四视图 golden 全绿；键盘全流程可操作；动效只在三个决定性瞬间出现 |
| **0.9** | 包生态 | 导入 `.mrpack`/CF zip/FTB 包；启动器实例发现（`.minecraft`/Prism/HMCL/PCL）；**自包含导出**（`SelfContainedRenderer` + 许可闸门 §3.5 + NOTICE + `redistribution.json`，导入端也校验） | 三种格式导入成功率 ≥ 95%（各 10 个真实包）；自包含包在另一台机器上脱离网络可启动；禁止再分发的 mod 在 UI 上被拦下且有解释 |
| **1.0** | 发行 | 签名/图标/多语言（zh/en，Qt Linguist）、性能预算、文档与迁移指南、Flatpak 真机验证 | 启动 < 1 s、点菜首屏 < 300 ms、内存 < 200 MB；三平台安装包齐 |

**依赖关系**：0.5（地基+主题）→ 0.6（上菜）与 0.7（RAG）**可并行** → 0.8（UI）依赖前两者定型 →
0.9 依赖 0.6。**注**：0.6 的上菜页先用 `qml2` 的雏形实现（ServeView + RippleCanvas），0.8 再统一到
完整组件库——不然水波特效要写两遍。

**不做什么**（明确划掉，防止范围蔓延）：不内置 Java 运行时安装、不做 Mojang 账号登录、不自建
mod 托管/CDN、不做 Forge/NeoForge 安装器集成（要起 JVM，单独立项）、不做 macOS 的 GUI 打磨、
不做壁纸取色（C 方案推迟到 1.1）。

---

## 6. 风险登记

| 风险 | 影响 | 缓解 |
|---|---|---|
| CurseForge 无 API key（403） | 0.9 的 CF 包导入可能只能靠 cfwidget 猜 | 先支持 mrpack + FTB；CF 导入降级为「只解析 manifest 并提示手动补文件」 |
| 再分发许可 | 自包含包有法律风险 | §3.5 闸门：白名单自动通过、copyleft 需确认、`LicenseRef-*` 默认拒绝；NOTICE + `redistribution.json` 可追溯；导入端二次校验 |
| RAG 包体 | 安装包 73 → ~185 MB | int8 量化（向量 19 MB 而非 77 MB）+ 文本/ FTS 压缩；出口设 ≤200 MB 硬线，超了就砍摘录文本只留短描述 |
| 水波特效在软件渲染/集显上掉帧 | 0.6 出口不达标 | 三条降级路径（Software API / 帧时监控 / reduceMotion），出口前必须在 Intel 集显上测 |
| UI 重构范围蔓延 | 0.8 无限延期 | 先定 `ui-spec-1.0.md` 与四个视图（Order/Cook/Serve/Settings）的验收清单，新增视图一律进 1.1 |
| 三平台 CI 无真机 GPU | offscreen 只能验渲染逻辑 | CI 只跑离屏 + golden；真机 GPU 冒烟在发布前手动跑 |

---

## 7. 工时估算（一人，按工作日）

| 阶段 | 估算 | 备注 |
|---|---|---|
| 0.5 平台层 + Windows 修复 + **色板 B** + CI | 13–15 天 | HCT 生成与对比度门禁占 4–5 天，CI 首次配置 2 天 |
| 0.6 PackModel + 两个出口 + 上菜页 + 动效 v1 | 12–15 天 | 事务化安装/回滚与 shader 各占一半 |
| 0.7 RAG v1 | 16–21 天 | 语料/评测集占一半，量化 + 打包 +1 天 |
| 0.8 UI 重构 | 15–18 天 | 组件库 + golden 测试 |
| 0.9 包生态 + 导入 + **自包含导出** | 10–13 天 | 许可闸门 2 天，真实包兼容性测试不可压缩 |
| 1.0 打磨 + 三平台发布 | 8–10 天 | 签名/文档/多语言 |
| 合计 | **74–92 天** | 不含 Forge/NeoForge 安装器 |

---

## 8. 决议（全部已定，2026-10-21）

| # | 决议 | 落点 |
|---|---|---|
| 1 | 色板 **B 方案**，A 为运行时回退，C 推迟到 1.1 | §1.3，0.5 |
| 2 | 产出界面 **点菜 / 烹饪 / 上菜** 三段式，上菜页两出口平权 | §2.2、§3.2，0.6 |
| 3 | UI 与动效语言由本方案设计，节奏常量集中 `Motion.qml` | §2.4，0.6/0.8 |
| 4 | **RAG 嵌入模型内嵌** + 种子索引 int8 内置，首启零下载 | §4.2、§4.5，0.7 |
| 5 | **允许自包含包**，但必须过许可闸门（默认拒绝 + NOTICE + 可追溯） | §3.5，0.9 |

无待决项。下一步是把 0.5 拆成可执行清单（平台层接口冻结 → Windows 修复 → 色板 B → CI 门禁）。
