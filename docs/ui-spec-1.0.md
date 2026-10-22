# BMCD 1.0 界面规格（1.0 实现版）

本文记录 **已经落地** 的 `qml2/` 界面与交互，不是愿望清单。1.0 只有 `qml2/`
（QML 模块 `BMCD.UI`），v1 单文件界面已删除；未实现能力集中在 §9，能力状态与术语以
[`plan.md`](plan.md) 为准。

- 视觉基准：`docs/preview-{order,cook,serve,settings}.png`（1120×760，
  `QT_QPA_PLATFORM=offscreen` 实拍；`--target /tmp/BMCD-preview` 避免写入本机路径）
- 色板来源与算法：`docs/roadmap-1.0.md` §2、`src/theme/TokenBuilder.cpp`
- 动效常量唯一定义处：`qml2/Motion.qml`；语义色唯一定义处：`qml2/Tokens.qml`

## 1. 信息架构：点菜 / 烹饪 / 上菜

| 层 | 用户动作 | 界面 | 后端 |
|---|---|---|---|
| 点菜 Order | 选游戏版本、看版本详情 | `OrderView.qml` | `VersionService` / `VersionDetail` / `InstalledVersions` |
| 烹饪 Cook | 下载、暂停、看进度 | `CookView.qml` | `DownloadTask`（并发、分块、sha1 校验） |
| 上菜 Serve | 出菜到文件夹 / 整合包 | `ServeView.qml` | `PackController` → `Deployer` / `Renderers` / `LicenseGate` |
| 设置 Settings | 看路径、版本、色板模式 | `SettingsView.qml` | `platform::Paths` / `Palette` |

左侧 `NavRail` 四项一一对应，`Ctrl+1..4` 直接跳转。四个视图由 `Loader.active` 切换，
同一时刻只加载一个；页面状态放在 `VersionDetail` / `PackController` 等单例里，切页不丢。

点菜页当前搜索框只搜版本号/类型；1.1 将接入**基础查表**（本地别名/分类表 + Modrinth
名称/分类查询，按 loader/版本过滤），不加载向量模型、不显示索引/相关度，也没有 RAG 开关。
查询无结果时给“换关键词/检查 loader 或版本”的可操作提示，不假装语义理解。

点菜页的模组勾选与菜篮见 `plan.md` §1.1；当前没有逐文件选择，也没有 RAG 推荐卡片。

## 2. Design token

语义色全部来自 `Palette`，`Tokens.qml` 只是加别名，QML 里**禁止**出现十六进制色值。

| token | 用途 | 暗色取值来源 |
|---|---|---|
| `background` | 窗口底色 | `#0E1415`（fallback）或 L\*7 |
| `surface` / `surfaceAlt` / `surfaceHigh` | 卡片 / 输入框 / 悬浮层 | L\*7 / 11 / 15 |
| `text` / `textMuted` / `textVariant` | 正文 / 次要 / 弱化 | L\*94 / 78（-alpha）/ 78 |
| `primary` / `primaryInk` | 主按钮、进度 / 主按钮文字 | L\*80，chroma 10–44（跟随源色相） |
| `primaryContainer` / `primaryContainerInk` | 选中态、徽标 / 其上的文字 | L\*32 / L\*90 |
| `outline` | 1 px 描边 | L\*56 |
| `danger` / `warning` / `success` / `link` | 错误 / 警告 / 成功 / 链接 | 固定色相 25 / 80 / 148；对比度门禁 |

尺寸 token：间距 4/8/12/16/24/32，圆角 6/10/14/999，字号 11/13/16/22/30。
所有字号 ≥ 11，正文 13；不随窗口缩放，DPI 交给 Qt。

对比度门禁（`TokenBuilder::enforceContrast`）：正文 7:1、次要 4.5:1、强调 3:1、
`onPrimary` 4.5:1，共 12 组配对；任一组不达标 → 该 token 沿 L\* 步进，仍失败 →
整份回退 `fallbackTokens()`（A 方案），并在 `Palette.tokenMode` 里如实报告
（`accent:<hex>:dark|light` / `fallback-contrast` / `fallback-builtin`）。

## 3. 动效

`Motion.qml` 是全应用唯一的时间来源；`Palette.reduceMotion` 为真时全部塌缩为
≤120 ms 的淡入，弹簧动画禁用。

| 常量 | 常态 | 降级 | 用途 |
|---|---|---|---|
| `micro` | 120 ms | 90 ms | hover、按下、位移 |
| `standard` | 180 ms | 110 ms | 卡片入场、徽标跳动 |
| `screen` | 260 ms | 120 ms | 切页、抽屉 |
| `settle` | 700 ms | 120 ms | 进度条落定 |
| `celebrate` | 1000 ms | 120 ms | 上菜完成 |
| `stampSpring` | 220 ms | — | 「落章」：列表项落位、唯一允许用弹簧的地方 |

水波纹 `RippleCanvas.qml`：纯 Rectangle 两环，不用 shader（软件渲染也正确）；
汇聚＝进度推进（内环 3 圈 1200 ms、外环延迟 180 ms），扩散＝任务完成（2 环 900 ms）。

## 4. 组件契约

| 组件 | 关键属性/信号 | 状态 |
|---|---|---|
| `AppButton` | `text`、`primary`、`danger`、`enabled`、`clicked()` | 默认 / hover（surfaceHigh）/ 按下（scale 0.98 + 深一档）/ 禁用 |
| `SearchField` | `text`、`placeholder`、`cleared()` | 空 / 输入 / 聚焦（outline 变 primary） |
| `Panel` | `title`、默认 `contentItem` | 静态容器 |
| `ConfirmDialog` | `title`、`body`、`confirmText`、`accepted()` | 打开 / 取消；Esc 关闭 |
| `ToastBar` | `text`、`kind`（info/ok/error）、`show()` | 3 s 自动消失，可被新消息替换 |
| `CircleProgress` | `progress`（0–1）、`label` | 无限进度用旋转弧 |
| `RippleCanvas` | `progress`、`mode`（gather/spread） | 静态 / 汇聚 / 扩散 |
| `WindowChrome` | 标题、`min/max/close` 信号 | 拖拽移动、双击最大化、八向 resize |

`WindowChrome` 不再依赖系统标题栏（`Qt.FramelessWindowHint`），所以三键必须自绘：
本版已有 min/max/close，双击标题栏最大化，边角命中区 6 px。

## 5. 视图状态机

`OrderView` 用 `phase` 表示显式状态（QML `Item` 自带 `state` 属性，未限定绑定会被内建
空字符串遮蔽，所以不叫 `state`）：

```
0 empty    无版本 / 搜索无结果
1 loading  拉取版本清单
2 partial  预留：清单已入但详情未取；当前实现自动选中首项，只产生 0/1/3/4
3 error    VersionService.error 非空（文案 + 重试）
4 success  列表可用
```

`CookView` 读 `downloadState`：idle / running / paused / error / done；
暂停/继续/取消是同一排按钮，禁用态随状态切换。
`ServeView` 由 `PackController` 驱动：

```
idle → prepare(versionId, jsonUrl) → loading → ready
ready --preflight()--> ready(dryRunOk)
ready --serve()--> working(busy/taskDone/taskTotal) --> done | error
ready --exportPack(mrpack|ftb|self)--> working --> done(resultPath)
```

`licenceDeniedGroups` 非空时「自包含包」按钮禁用；闸门按父级模组聚合成
`licenceGroups`，点一次「已获授权」调用 `confirmGroup(groupId)` 覆盖该模组全部文件。
普通版本的 client/lib/asset 由启动器安装、不进入自包含包，不计入闸门。

## 6. 键盘与可访问性

- `Ctrl+1..4`：四个导航页；`/`：聚焦搜索框；`Esc`：关闭对话框 / 清空详情。
- 所有可聚焦控件有可见焦点环（outline → primary），Tab 顺序 = 视觉顺序。
- 不依赖颜色传达状态：错误有图标 + 文案，进度有百分比数字。
- 动效可被输入打断；`reduceMotion` 时不做位移与弹簧。
- 每屏最小 880×600；窗口 1120×760 时四个视图都无滚动条。
- 内容区最大 1280 px 并居中（超宽窗口不把表格拉成一条线）；边距 880–1000 px 时 12 px，
  更宽时 16 px。
- 面板高度由内容决定；设置页在高度不足时整页滚动。空态占满剩余高度并居中。
- 窗框八向可拖拽：四边 6 px、四角 16 px，最大化时禁用；标题栏双击切换最大化。

## 7. 离屏回归

界面回归不靠肉眼，靠像素：

```bash
QT_QPA_PLATFORM=offscreen MC_SCREENSHOT=/tmp/ui.png MC_SCREENSHOT_DELAY=7000 \
  ./build/bmcd
```

判据：尺寸 1120×760、左上角背景 `(14,20,21)`、颜色数 > 1000、`MC_LOG` 中无
`ReferenceError` / `TypeError` / `Unable to assign`。与 `docs/preview-order.png`
做 RMSE（阈值 0.02）与 2% fuzz 下的差异像素数（阈值 5000 px）——动态文本会让差异
非零，所以阈值放宽，但配色回归（整屏变色）必然超限。CI 里同等条件跑一次。

## 8. 1.0 界面重构（已落地）

- 图标：`qml2/ui/Icon.qml` 走 `IconImageProvider`（SVG → 按 palette 着色），不再用
  `▤ ≡ ↥ ⚙` 字体字形，避免不同字体的基线/宽度漂移；`--target` 截图和 golden 都可复现。
- 层级：`PageHeader`（22 px 标题 + 13 px 说明）、`SectionLabel`（11 px DemiBold）、
  `SettingRow`（92 px 标签 + 可省略值）统一四个视图；窗口按钮 12 px 图标，标题与副标题
  按 baseline 对齐。
- 响应：`OrderView` 左栏 `clamp(300, 38%, 460)`，搜索框占满、渠道按钮换行；详情操作栏
  贴底；`CookView`/`ServeView` 面板内容自适应；`SettingsView` 可滚动。
- QML 坑位：属性名以 `on` 开头会被当成信号处理器（`onPrimary` / `onPrimaryContainer`
  解析为黑色），因此 token 暴露为 `primaryInk` / `primaryContainerInk`。

## 9. 未做（明确记录）

- 无最小化到托盘、无系统主题实时跟随（只在启动时读一次，`Palette.reload()` 可手动触发）。
- 无高对比度模式；导航、窗口按钮、主操作按钮已接 `Accessible.name/role`，其余控件未系统化。
- 模组勾选、菜篮、依赖解析：未完成（1.1）；基础查表检索先接入，RAG 不在本轮。
- RAG 向量知识库、QueryPlan、语义重排与开关：延后到 1.6；当前没有 RAG 建议/推荐卡片，
  `SettingsView` 不展示索引状态。
- 自包含包（A 方案，目标机已有同版本原版）在离线机器导入/部署，mrpack/CF/FTB 各 20 个真实包：未执行。
- macOS 发行本轮暂缓；Flatpak、Windows 真机：未交付（见 `plan.md`）。
