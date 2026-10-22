# BMCD 打包

发布流水线按版本 tag 触发，见下文「Release workflow」。本目录保留本地调试和
CI 复用的具体脚本；正式发行物由 `.github/workflows/release.yml` 统一构建。

三条产物线共用 `packaging/bmcd.desktop`、`packaging/bmcd.svg` 和
`packaging/flatpak/org.bmcd.BMCD.metainfo.xml`；应用 id 统一为
`org.bmcd.BMCD`（窗口的 `DesktopFileName`、desktop 文件的 `Icon=` 都指向它）。

## 统一冒烟

```bash
packaging/smoke-artifact.sh binary  build/bmcd                 # 开发二进制
packaging/smoke-artifact.sh appimage dist/BMCD-x86_64.AppImage # 已打包 AppImage
packaging/smoke-artifact.sh flatpak                            # 已安装 Flatpak
```

三个模式都执行 `--version`、`--i18n-check` 和一次离屏 `--dump-layout` 启动；
任何一步失败返回非零。CI 的产物检查也走这个脚本。

## AppImage

```bash
packaging/build-appimage.sh                    # 编译 + 打包 + SHA256SUMS + 冒烟
BMCD_SKIP_BUILD=1 packaging/build-appimage.sh  # 复用现有 build/bmcd
```

产物：`build/BMCD-x86_64.AppImage`、`build/SHA256SUMS`。脚本默认要求构建机
glibc ≤ **2.35**（Ubuntu 22.04 基线），在 Arch 上直接构建会拒绝；只为本地诊断
生成不合规产物时才设 `BMCD_ALLOW_NEW_GLIBC=1`，不得发布。

CI 的 Linux job 固定在 `ubuntu-22.04`，因此 AppImage 不再绑定 Arch glibc。
工具放在 `.tools/`（不入库，缺失时脚本会报路径）：

| 文件 | 用途 |
| --- | --- |
| `linuxdeploy-x86_64.AppImage` | 收集依赖、生成 AppDir |
| `linuxdeploy-plugin-qt-x86_64.AppImage` | 部署 Qt 库、QML 模块与插件 |
| `appimagetool-x86_64.AppImage` | 把 AppDir 压成 AppImage |

`appimagetool` 默认会从 GitHub 下载 runtime；离线环境可把它缓存到本地并设置
`BMCD_APPIMAGE_RUNTIME=/path/to/runtime-x86_64`，脚本会改用 `--runtime-file`。

这台机器没有 `/dev/fuse`，脚本统一用 `APPIMAGE_EXTRACT_AND_RUN=1` 调用工具；
运行产物时同理：

```bash
APPIMAGE_EXTRACT_AND_RUN=1 ./dist/BMCD-x86_64.AppImage --plan 26.3
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

`dist/BMCD-x86_64.AppImage` 是旧 Arch 构建的快照，不满足 2.35 基线；1.5 的发布
AppImage 以 CI 的 ubuntu-22.04 产物为准。Qt 动态链接，`.so` 都在 AppImage 内，
符合 LGPLv3；不要改成静态 Qt。

## Flatpak

```bash
packaging/build-flatpak.sh                 # 构建 + 用户级安装 + bundle + 冒烟
packaging/build-flatpak.sh --no-install    # 只构建 + bundle
packaging/build-flatpak.sh --working-tree  # 仅本地：打包未提交工作区
```

需要 `flatpak-builder` 以及 `org.kde.Platform//6.9` / `org.kde.Sdk//6.9`：

```bash
flatpak install flathub org.kde.Platform//6.9 org.kde.Sdk//6.9
flatpak run org.bmcd.BMCD --plan 26.3
```

清单的 `sources` 是 `type: git` + 固定 commit，不再指向工作区。脚本会把该 commit
替换成当前 checkout 的 `HEAD` 再生成构建用清单，因此干净 checkout 构建的是它自己的
commit；`--working-tree` 用 `type: dir` 临时打包未提交内容，`BMCD_RELEASE=1` 时拒绝。
产物：`build/BMCD-x86_64.flatpak` 和 `build/SHA256SUMS`。

**本环境未验证**：开发容器里没有 `flatpak-builder`（也没有 root 权限安装），
KDE runtime 也没装，所以清单还没有在本机真正构建过。CI 的 `flatpak` job 会在
Ubuntu 24.04 上安装工具和 runtime，执行 `--no-install --working-tree` 并校验 bundle；
首次真实构建以该 job 的日志为准。

清单要点：

- `finish-args` 里给了网络、Wayland/X11、DRI 和 `home` 文件系统（下载目标默认在 home）。
- 只读挂载 `xdg-config/kdeglobals` 与 `xdg-data/color-schemes`，否则读不到 matugen 色板，
  窗口会退回默认配色。
- `type: git` 的 commit 在发布 tagging 时更新；不要改回 `type: dir` 进发布产物。

## Arch Linux

```bash
packaging/arch/build-package.sh --version 1.0.0
```

脚本把工作区打成源码归档，在 `build/archpkg/` 里用 `makepkg` 构建，产物为
`build/arch/bmcd-<version>-1-x86_64.pkg.tar.zst` 和 `SHA256SUMS`。在 Arch 容器里以
root 调用时会自动切换到临时 `builder` 用户。发行包依赖系统 `qt6-base`、
`qt6-declarative`、`qt6-svg`，不把 Qt 塞进包内。

## Debian / Ubuntu

```bash
packaging/build-appimage.sh                    # 先产出 build/AppDir
packaging/debian/build-deb.sh --version 1.0.0
```

`.deb` 把 AppDir 安装到 `/opt/bmcd`，通过 `/usr/bin/bmcd -> /opt/bmcd/AppRun`
启动，因此不要求系统 Qt 版本。脚本优先调用 `dpkg-deb`；没有 `dpkg-deb` 时也会
生成可检查的 ar 格式包。产物：`build/debian/bmcd_<version>_amd64.deb` 和
`SHA256SUMS`。

## Windows

```powershell
pwsh packaging/build-windows.ps1
pwsh packaging/build-windows.ps1 -Sign -CertificateThumbprint <thumbprint>
pwsh packaging/windows/build-installer.ps1 -Version 1.0.0
```

在 Windows runner 完成 `cmake --build build --config Release` 后运行；要求
`windeployqt` 在 PATH。`build-windows.ps1` 用 `--compiler-runtime` 收集 VC runtime，
检查 `platforms/qwindows.dll`、`Qt6Core.dll`、`Qt6Quick.dll`，产出：

- `dist/BMCD-windows-x64.zip`
- `dist/BMCD/`（安装器输入目录）
- `dist/SHA256SUMS.txt`
- 未签名时 `dist/BMCD/UNSIGNED.txt`

`build-installer.ps1` 调用 Inno Setup 6 的 `iscc`，生成
`dist/BMCD-Setup-<version>-x64.exe`，并把安装包 SHA256 追加到 `SHA256SUMS.txt`。
无证书的构建**明确标记未签名**，不满足 1.5 的签名验收；有证书时才用
`signtool sign/verify`。路径含空格/中文、长路径、代理和杀软需要在干净 Windows 11
VM 手工验证，本仓库没有 Windows VM，不能拿 CI 启动代替。

## Release workflow

`.github/workflows/release.yml` 只在推送 `v*` 标签时触发：

1. `prepare` 校验 tag 是 `vX.Y.Z` 且与 `CMakeLists.txt` 的 `project(... VERSION ...)` 一致；
2. `arch` 在 `archlinux:base-devel` 容器里构建、安装并冒烟 Arch 包；
3. `debian` 在 Ubuntu 22.04 构建 AppDir、`.deb`，安装后冒烟；
4. `windows` 在 windows-2022 构建 Release、便携包、Inno Setup 安装包；
5. `release` 汇总全部产物，生成总 `SHA256SUMS`，用 `gh release create --verify-tag --generate-notes` 发布。

发布前请先确认本地 `ctest` 通过，再推 tag：

```bash
git tag -a v1.0.0 -m "BMCD 1.0.0"
git push origin v1.0.0
```

workflow 使用仓库自带的 `GITHUB_TOKEN`（`contents: write`），不需要额外的 PAT。

## CI

`.github/workflows/ci.yml`：

- Linux（ubuntu-22.04）：构建、单测、离屏 UI、AppImage + `SHA256SUMS`；
- Flatpak（ubuntu-24.04）：装 KDE SDK、构建 bundle、校验 sha256、上传；
- Windows（windows-2022）：`build-windows.ps1` 便携包 + `SHA256SUMS.txt`；
- macOS：只保留编译、单测和离屏冒烟，不产出 dmg，不进入 1.5 验收。

所有产物未签名时都必须如实标出；AppImage/Flatpak 的 `SHA256SUMS` 和 Windows 的
`SHA256SUMS.txt` 随 artifact 上传。
