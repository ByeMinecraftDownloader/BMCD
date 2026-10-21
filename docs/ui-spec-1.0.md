# BMCD 1.0 界面规格（0.6 实现版）

本文记录 **已经落地** 的界面与交互，不是愿望清单。凡标「冻结」的部分（`qml/` 目录下的
v1 界面）在 1.0 已移除；`qml2/`（QML 模块 `BMCD.UI`）是唯一界面
切换，默认 v2。

- 视觉基准：`docs/preview-06.png`（1120×760，`QT_QPA_PLATFORM=offscreen` 实拍）
- 色板来源与算法：`docs/roadmap-1.0.md` §2、`src/theme/TokenBuilder.cpp`
- 动效常量唯一定义处：`qml2/Motion.qml`；语义色唯一定义处：`qml2/Tokens.qml`

## 1. 信息架构：点菜 / 烹饪 / 上菜

| 层 | 用户动作 | 界面 | 后端 |
|---|---|---|---|
| 点菜 Order | 选版本、「挑模组」 | `OrderView.qml` | `VersionService` / `VersionDetail` / `InstalledVersions` |
| 烹饪 Cook | 下载、暂停、看进度 | `CookView.qml` | `DownloadTask`（并发、分块、sha1 校验） |
| 上菜 Serve | 出菜到文件夹 / 整合包 | `ServeView.qml` | `PackController` → `Deployer` / `Renderers` / `LicenseGate` |
| 设置 Settings | 看路径、版本、色板模式、检索索引 | `SettingsView.qml` | `platform::Paths` / `Palette` / `RagController` |

左侧 `NavRail` 四项一一对应，`Ctrl+1..4` 直接跳转。四个视图常驻（`Loader` + `visible`），
不销毁，避免切页丢状态。

点菜页搜索框下方有一行**检索建议**（`RagController.suggest`，260 ms 防抖，最多 3 条，
显示「相关：标题 — 片段」）。建议只做提示，不改变列表过滤；索引不存在时静默隐藏，
设置页显示「未建立（bmcd --rag-build）」。

## 2. Design token

语义色全部来自 `Palette`，`Tokens.qml` 只是加别名，QML 里**禁止**出现十六进制色值。

| token | 用途 | 暗色取值来源 |
|---|---|---|
| `background` | 窗口底色 | `#0E1415`（fallback）或 L\*7 |
| `surface` / `surfaceAlt` / `surfaceHigh` | 卡片 / 输入框 / 悬浮层 | L\*7 / 11 / 15 |
| `text` / `textMuted` / `textVariant` | 正文 / 次要 / 弱化 | L\*94 / 78（-alpha）/ 78 |
| `primary` / `onPrimary` | 主按钮、进度 | L\*80，chroma 10–44（跟随源色相） |
| `primaryContainer` / `onPrimaryContainer` | 选中态、徽标 | L\*32 / L\*90 |
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

`OrderView` 的 `state` 是显式五态，便于测试与文案：

```
0 empty    无版本 / 搜索无结果
1 loading  拉取版本清单
2 partial  清单已入但详情未取（0.7 起用于模组列表流式加载）
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

`licenceDenied` 非空时「自包含包」按钮禁用，需逐文件 `confirm(path)`（对话框内列出被拒文件）。

## 6. 键盘与可访问性

- `Ctrl+1..4`：四个导航页；`/`：聚焦搜索框；`Esc`：关闭对话框 / 清空详情。
- 所有可聚焦控件有可见焦点环（outline → primary），Tab 顺序 = 视觉顺序。
- 不依赖颜色传达状态：错误有图标 + 文案，进度有百分比数字。
- 动效可被输入打断；`reduceMotion` 时不做位移与弹簧。
- 每屏最小 880×600；窗口 1120×760 时四个视图都无滚动条。

## 7. 离屏回归

界面回归不靠肉眼，靠像素：

```bash
QT_QPA_PLATFORM=offscreen MC_SCREENSHOT=/tmp/ui.png MC_SCREENSHOT_DELAY=7000 \
  ./build/bmcd --ui=v2
```

判据：尺寸 1120×760、左上角背景 `(14,20,21)`、颜色数 > 1000、`MC_LOG` 中无
`ReferenceError` / `TypeError` / `Unable to assign`。与 `docs/preview-06.png`
做 RMSE（阈值 0.02）与 2% fuzz 下的差异像素数（阈值 5000 px）——动态文本会让差异
非零，所以阈值放宽，但配色回归（整屏变色）必然超限。CI 里同等条件跑一次。

## 8. 未做（明确记录）

- 无最小化到托盘、无系统主题实时跟随（只在启动时读一次，`Palette.reload()` 可手动触发）。
- 无高对比度模式、无屏幕阅读器语义（`Accessible.*` 未接）。
- 0.7 的模组列表、RAG 搜索框、「推荐」卡片只留了版位（`OrderView` 右栏 + `SettingsView`
  的 RAG 占位）。
