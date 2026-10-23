import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs
import BMCD as Bmcd
import BMCD.Settings 1.0
import BMCD.UI

Item {
    id: view

    property bool showDiagnostics: false
    property bool javaProbed: false

    function displayPath(path) {
        return String(path || "").split(homeDir).join("~");
    }

    function probeJava(force) {
        if (!force && javaProbed)
            return;
        javaProbed = true;
        if (force)
            Bmcd.JavaLocator.detectAgain();
        else
            Bmcd.JavaLocator.detect();
    }

    Component.onCompleted: if (visible) probeJava(false)
    onVisibleChanged: if (visible) probeJava(false)

    FolderDialog {
        id: targetDialog
        title: qsTr("选择游戏目录")
        currentFolder: "file://" + AppSettings.targetDir
        onAccepted: AppSettings.targetDir = AppSettings.localPath(selectedFolder)
    }

    FileDialog {
        id: javaDialog
        title: qsTr("选择 Java 可执行文件")
        nameFilters: [qsTr("Java 可执行文件 (java java.exe)"), qsTr("所有文件 (*)")]
        onAccepted: {
            AppSettings.javaPath = AppSettings.localPath(selectedFile);
            probeJava(true);
        }
    }

    FolderDialog {
        id: tempDirDialog
        title: qsTr("选择临时下载目录")
        currentFolder: "file://" + (AppSettings.downloadTempDir.length > 0
                                     ? AppSettings.downloadTempDir : homeDir)
        onAccepted: AppSettings.downloadTempDir = AppSettings.localPath(selectedFolder)
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: contentColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        Column {
            id: contentColumn
            width: parent.width
            spacing: Tokens.spacingLg

            PageHeader {
                width: parent.width
                title: qsTr("设置")
                subtitle: qsTr("路径、语言与下载行为；改完立即生效。")
            }

            Panel {
                id: runtimePanel
                width: parent.width
                height: runtimeColumn.implicitHeight + padding * 2

                Column {
                    id: runtimeColumn
                    width: parent.width
                    spacing: Tokens.spacingMd

                    SectionLabel { text: qsTr("运行环境") }

                    Row {
                        width: parent.width
                        height: 36
                        spacing: Tokens.spacingMd

                        Text {
                            width: 92
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("游戏目录")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                        Text {
                            width: parent.width - 92 - chooseDir.width - Tokens.spacingMd * 2
                            anchors.verticalCenter: parent.verticalCenter
                            text: view.displayPath(AppSettings.targetDir)
                            color: Tokens.text
                            font.pixelSize: Tokens.fontSizeBody
                            elide: Text.ElideMiddle
                        }
                        AppButton {
                            id: chooseDir
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("选择…")
                            iconName: "folder"
                            onClicked: targetDialog.open()
                        }
                    }

                    Row {
                        width: parent.width
                        height: 36
                        spacing: Tokens.spacingMd

                        Text {
                            width: 92
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("临时下载目录")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                            wrapMode: Text.WordWrap
                        }
                        Text {
                            width: parent.width - 92 - chooseTempDir.width - defaultTempDir.width
                                   - Tokens.spacingMd * 3
                            anchors.verticalCenter: parent.verticalCenter
                            text: view.displayPath(AppSettings.downloadRoot)
                            color: Tokens.text
                            font.pixelSize: Tokens.fontSizeBody
                            elide: Text.ElideMiddle
                        }
                        AppButton {
                            id: chooseTempDir
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("选择…")
                            iconName: "folder"
                            onClicked: tempDirDialog.open()
                        }
                        AppButton {
                            id: defaultTempDir
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("使用默认")
                            enabled: AppSettings.downloadTempDir.length > 0
                            onClicked: AppSettings.downloadTempDir = ""
                        }
                    }

                    Text {
                        width: parent.width
                        text: qsTr("下载中的文件先写入这里；选择后使用其下的 BMCD/downloads 子目录。")
                        color: Tokens.textMuted
                        font.pixelSize: Tokens.fontSizeCaption
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        width: parent.width
                        height: 36
                        spacing: Tokens.spacingMd

                        Text {
                            width: 92
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Java")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                        Text {
                            width: parent.width - 92 - chooseJava.width - detectJava.width
                                   - autoJava.width - Tokens.spacingMd * 4
                            anchors.verticalCenter: parent.verticalCenter
                            text: Bmcd.JavaLocator.checking ? qsTr("检测中…")
                                  : Bmcd.JavaLocator.available
                                    ? view.displayPath(Bmcd.JavaLocator.path)
                                    : qsTr("未找到可用 Java")
                            color: Bmcd.JavaLocator.available ? Tokens.text : Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeBody
                            elide: Text.ElideMiddle
                        }
                        AppButton {
                            id: chooseJava
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("选择…")
                            iconName: "folder"
                            enabled: !Bmcd.JavaLocator.checking
                            onClicked: javaDialog.open()
                        }
                        AppButton {
                            id: detectJava
                            anchors.verticalCenter: parent.verticalCenter
                            text: Bmcd.JavaLocator.checking ? qsTr("检测中…") : qsTr("重新检测")
                            iconName: "refresh"
                            enabled: !Bmcd.JavaLocator.checking
                            onClicked: probeJava(true)
                        }
                        AppButton {
                            id: autoJava
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("自动")
                            enabled: AppSettings.javaPath.length > 0
                                     && !Bmcd.JavaLocator.checking
                            onClicked: {
                                AppSettings.javaPath = "";
                                probeJava(true);
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        text: Bmcd.JavaLocator.checking ? qsTr("正在查找 Java…")
                              : Bmcd.JavaLocator.available
                                ? qsTr("Java %1 · %2").arg(Bmcd.JavaLocator.version)
                                                       .arg(Bmcd.JavaLocator.source)
                                : Bmcd.JavaLocator.error.length > 0
                                  ? Bmcd.JavaLocator.error
                                  : qsTr("尚未检测；可手动选择 java 可执行文件。")
                        color: Bmcd.JavaLocator.available || Bmcd.JavaLocator.checking
                               ? Tokens.textMuted : Tokens.danger
                        font.pixelSize: Tokens.fontSizeCaption
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        width: parent.width
                        height: 36
                        spacing: Tokens.spacingMd

                        Text {
                            width: 92
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("界面语言")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                        ComboBox {
                            id: languageBox
                            width: 150
                            anchors.verticalCenter: parent.verticalCenter
                            model: [qsTr("简体中文"), "English"]
                            currentIndex: AppSettings.language.startsWith("en") ? 1 : 0
                            onActivated: function(index) {
                                AppSettings.language = index === 1 ? "en" : "zh_CN";
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("切换后立即刷新界面")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                    }

                    Row {
                        width: parent.width
                        height: 36
                        spacing: Tokens.spacingMd

                        Text {
                            width: 92
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("下载并发")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                        SpinBox {
                            id: concurrencyBox
                            width: 120
                            anchors.verticalCenter: parent.verticalCenter
                            from: 1
                            to: 16
                            value: AppSettings.downloadConcurrency
                            onValueModified: AppSettings.downloadConcurrency = value
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("同时下载的文件数（1–16）")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                    }

                    Row {
                        width: parent.width
                        height: 36
                        spacing: Tokens.spacingMd

                        Text {
                            width: 92
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("减少动效")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                        Switch {
                            id: motionSwitch
                            anchors.verticalCenter: parent.verticalCenter
                            checked: AppSettings.reduceMotion
                            onToggled: AppSettings.reduceMotion = checked
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("关闭水波、弹跳等装饰动画")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                    }
                }
            }

            Panel {
                id: sourcesPanel
                width: parent.width
                height: sourcesColumn.implicitHeight + padding * 2

                Column {
                    id: sourcesColumn
                    width: parent.width
                    spacing: Tokens.spacingMd

                    SectionLabel { text: qsTr("下载源与灾备") }

                    Row {
                        width: parent.width
                        height: 36
                        spacing: Tokens.spacingMd

                        Text {
                            width: 92
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("模组 CDN")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                        SearchField {
                            id: cdnField
                            width: parent.width - 92 - Tokens.spacingMd
                            anchors.verticalCenter: parent.verticalCenter
                            text: AppSettings.cdnBase
                            placeholder: qsTr("留空使用 Modrinth 官方 CDN")
                            onEditingFinished: AppSettings.cdnBase = text
                        }
                    }

                    Row {
                        width: parent.width
                        height: 36
                        spacing: Tokens.spacingMd

                        Text {
                            width: 92
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Mojang 镜像")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                        SearchField {
                            id: mirrorField
                            width: parent.width - 92 - Tokens.spacingMd
                            anchors.verticalCenter: parent.verticalCenter
                            text: AppSettings.mirrorBase
                            placeholder: qsTr("留空使用官方源，例如 https://bmclapi2.bangbang93.com")
                            onEditingFinished: AppSettings.mirrorBase = text
                        }
                    }

                    Text {
                        width: parent.width
                        text: qsTr("镜像填入前缀即可；BMCLAPI 会自动映射资源、依赖库与客户端。")
                        color: Tokens.textMuted
                        font.pixelSize: Tokens.fontSizeCaption
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        width: parent.width
                        height: 36
                        spacing: Tokens.spacingMd

                        Text {
                            width: 92
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("下载灾备")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                        Switch {
                            id: failoverSwitch
                            anchors.verticalCenter: parent.verticalCenter
                            checked: AppSettings.mirrorFailover
                            onToggled: AppSettings.mirrorFailover = checked
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("主源失败后自动切换回官方源")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                    }

                    Row {
                        width: parent.width
                        spacing: Tokens.spacingMd

                        AppButton {
                            text: qsTr("恢复默认")
                            iconName: "refresh"
                            onClicked: {
                                AppSettings.cdnBase = "";
                                AppSettings.mirrorBase = "";
                                cdnField.text = "";
                                mirrorField.text = "";
                            }
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("设置会立即用于下一次下载")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                    }
                }
            }

            Panel {
                id: maintenancePanel
                width: parent.width
                height: maintenanceColumn.implicitHeight + padding * 2

                Column {
                    id: maintenanceColumn
                    width: parent.width
                    spacing: Tokens.spacingMd

                    SectionLabel { text: qsTr("诊断与维护") }

                    Row {
                        spacing: Tokens.spacingMd
                        AppButton {
                            text: qsTr("刷新版本清单")
                            iconName: "refresh"
                            onClicked: VersionService.refresh()
                        }
                        AppButton {
                            text: qsTr("重新扫描已安装")
                            iconName: "search"
                            onClicked: Bmcd.InstalledVersions.rescan()
                        }
                        AppButton {
                            text: view.showDiagnostics ? qsTr("收起详情") : qsTr("显示详情")
                            iconName: view.showDiagnostics ? "chevron" : "info"
                            onClicked: view.showDiagnostics = !view.showDiagnostics
                        }
                    }

                    Column {
                        width: parent.width
                        spacing: Tokens.spacingSm
                        visible: view.showDiagnostics

                        SettingRow { label: qsTr("版本"); value: Qt.application.version }
                        SettingRow { label: qsTr("主题")
                                     value: qsTr("%1（%2）").arg(Bmcd.Palette.tokenMode).arg(Bmcd.Palette.schemeName) }
                        SettingRow { label: qsTr("实际目录"); value: view.displayPath(AppSettings.targetDir) }
                    }
                }
            }
        }
    }
}
