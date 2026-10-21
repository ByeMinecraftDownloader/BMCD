# BMCD 打包

两条产物线共用 `packaging/bmcd.desktop`、`packaging/bmcd.svg` 和
`packaging/flatpak/org.bmcd.BMCD.metainfo.xml`；应用 id 统一为
`org.bmcd.BMCD`（窗口的 `DesktopFileName`、desktop 文件的 `Icon=` 都指向它）。

## AppImage

```bash
packaging/build-appimage.sh                    # 编译 + 打包
BMCD_SKIP_BUILD=1 packaging/build-appimage.sh  # 复用现有 build/bmcd
```

产物：`build/BMCD-x86_64.AppImage`（约 83 MB，含 Qt 运行库，不含可选 RAG 模型）。

CI 会在 Linux job 末尾自动下载 `.tools/` 工具并产出 AppImage；Windows job 产出
`BMCD-windows-x64.zip`（windeployqt 便携包），macOS job 产出 `build/bmcd.dmg`
（macdeployqt）。三平台产物均未签名。

工具放在 `.tools/`（不入库，缺失时脚本会报路径）：

| 文件 | 用途 |
| --- | --- |
| `linuxdeploy-x86_64.AppImage` | 收集依赖、生成 AppDir |
| `linuxdeploy-plugin-qt-x86_64.AppImage` | 部署 Qt 库、QML 模块与插件 |
| `appimagetool-x86_64.AppImage` | 把 AppDir 压成 AppImage |

这台机器没有 `/dev/fuse`，脚本内部统一用 `APPIMAGE_EXTRACT_AND_RUN=1` 调用这些工具；
运行产物时同理：

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./build/BMCD-x86_64.AppImage --plan 26.3
```

### 脚本里几个不得已的开关

- **`QMAKE=qmake6`**：Arch 同时装了 `qmake-qt5`，Qt 部署插件会先探测 Qt 5 然后报
  `Could not find Qt modules to deploy`。
- **自制插件根目录**：把 `/usr/lib/qt6/plugins` 硬链接到 `build/qt-plugins`，删掉 `ldd`
  有 `not found` 的插件（Arch 的 `kimg_jxr.so` 缺 `libjpegxr.so`，会让整个部署 abort），
  再用 qmake wrapper 把 `QT_INSTALL_PLUGINS:` 指过去。
- **`NO_STRIP=1`**：linuxdeploy 自带的 strip 是旧版 binutils，遇到 Arch 工具链生成的
  `.relr.dyn` 会报 `unknown type [0x13] section` 并让部署失败。
- **自写 `AppRun`**：linuxdeploy 默认把 `AppRun` 做成指向二进制的符号链接；AppImage
  runtime 按绝对路径 exec 它时，Qt 会用 `argv[0]` 推应用目录，找不到 `usr/bin/qt.conf`，
  插件路径落到载荷根目录，结果 `No functional TLS backend was found`、下载直接失败。
  现在 `AppRun` 是真正的启动脚本，显式导出 `LD_LIBRARY_PATH` / `QT_PLUGIN_PATH` /
  `QML2_IMPORT_PATH` 后再 `exec`。
- **额外部署 `libqoffscreen.so` / `libqminimal.so`**：linuxdeploy 只部署构建机匹配的平台
  插件，补上以后可以在没有显示器的地方跑 `QT_QPA_PLATFORM=offscreen` 冒烟测试。

### 已知限制

- 本机是 Arch（glibc 2.43），产物依赖宿主的 glibc。要给老发行版用，应在 Ubuntu 22.04
  容器里重打，或直接用下面的 Flatpak。
- Qt 动态链接，`.so` 都在 AppImage 内，符合 LGPLv3；不要改成静态 Qt。

## Flatpak

```bash
packaging/build-flatpak.sh            # 构建 + 用户级安装
packaging/build-flatpak.sh --no-install
```

需要 `flatpak-builder` 以及 `org.kde.Platform//6.9` / `org.kde.Sdk//6.9`：

```bash
flatpak install flathub org.kde.Platform//6.9 org.kde.Sdk//6.9
flatpak run org.bmcd.BMCD --plan 26.3
```

**本环境未验证**：开发容器里没有 `flatpak-builder`（也没有 root 权限安装），
KDE runtime 也没装，所以清单是按 Qt 6 / KDE SDK 的惯例写的，还没有真正构建过。
第一次在有 flatpak 的机器上跑时，请先 `--no-install` 看构建输出。

清单要点：

- `finish-args` 里给了网络、Wayland/X11、DRI 和 `home` 文件系统（下载目标默认在 home）。
- 只读挂载 `xdg-config/kdeglobals` 与 `xdg-data/color-schemes`，否则读不到 matugen 色板，
  窗口会退回默认配色。
- `sources` 用 `type: dir` 指向仓库根（项目还不是 git 仓库）；进 git 后换成 `type: git`。
