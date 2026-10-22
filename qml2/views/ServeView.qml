import QtQuick
import QtQuick.Dialogs
import BMCD
import BMCD.UI

// "出餐": inspect the cooked dish, then choose one of three exits. This screen
// is the authorisation point: preflight has run, the dish is cooked, and the
// licence gate is visible before anything is written.
Item {
    id: view

    readonly property int phase: {
        if (PackController.state === "error")
            return 3;
        if (PackController.state === "loading" || PackController.state === "working")
            return 1;
        if (PackController.fileCount === 0)
            return 0;
        return 4;
    }

    readonly property bool ready: phase === 4 && PackController.cooked
                                  && !PackController.busy
                                  && PackController.missingFiles.length === 0
    readonly property bool selfBlocked: PackController.licenceDeniedGroups > 0
    readonly property bool loaderReady: CookSession.loaderReady
    readonly property string servedLoader: {
        const type = PackController.loaderType;
        if (type.length > 0 && type !== "vanilla")
            return PackController.loaderVersion.length > 0
                   ? type + " " + PackController.loaderVersion : type;
        return "";
    }
    readonly property string blockedReason: {
        if (PackController.missingFiles.length > 0)
            return qsTr("还缺 %1 个文件，先回烹饪页完成下载。").arg(PackController.missingFiles.length);
        if (!PackController.cooked)
            return qsTr("这桌菜还没烹饪完成，先去烹饪页。");
        if (CookSession.requiredLoader.length > 0 && !CookSession.loaderSelectedForPlan)
            return qsTr("这个整合包需要 %1 加载器：先回点菜页选择。")
                   .arg(CookSession.requiredLoader);
        if (CookSession.hasLoader && !CookSession.loaderInstalled)
            return qsTr("加载器还没装好：先回烹饪页完成“加载器”步骤。");
        return "";
    }

    signal openOrder()

    // A licence row is one parent mod, not one jar: group ids are stable and
    // confirming one covers every file that belongs to it.
    function licenceGroupLabel(group) {
        if (group.kind === "minecraft")
            return qsTr("Minecraft 本体");
        if (group.kind === "directory")
            return qsTr("%1 目录").arg(group.name);
        return group.name.length > 0 ? group.name : group.id;
    }

    function licenceGroupDetail(group) {
        const license = group.licenseName.length > 0 ? group.licenseName : group.licenseId;
        if (license.length === 0)
            return qsTr("未标注可再分发许可 · %1 个文件").arg(group.pendingCount);
        return qsTr("%1 · %2 个文件").arg(license).arg(group.pendingCount);
    }

    function prepareDish() {
        if (VersionDetail.state !== "ready")
            return;
        if (VersionDetail.downloadState === "done")
            PackController.prepareDownloaded(VersionDetail.versionId,
                                             VersionService.urlFor(VersionDetail.versionId),
                                             VersionDetail.targetDir);
        else
            PackController.prepare(VersionDetail.versionId,
                                   VersionService.urlFor(VersionDetail.versionId));
    }

    function localPath(url) {
        let path = String(url);
        if (path.startsWith("file://"))
            path = path.substring(7);
        if (/^\/[A-Za-z]:/.test(path))
            path = path.substring(1);
        return decodeURIComponent(path);
    }

    function openServeFolder() {
        folderDialog.open();
    }

    FolderDialog {
        id: folderDialog
        title: qsTr("选择出菜目录")
        onAccepted: {
            PackController.deployDir = view.localPath(selectedFolder);
            PackController.serve();
        }
    }

    FileDialog {
        id: mrpackDialog
        title: qsTr("保存整合包")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mrpack"
        nameFilters: [qsTr("Modrinth 整合包 (*.mrpack)")]
        onAccepted: PackController.exportPackTo("mrpack", view.localPath(selectedFile))
    }

    FileDialog {
        id: selfDialog
        title: qsTr("保存自包含包")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "zip"
        nameFilters: [qsTr("ZIP 压缩包 (*.zip)")]
        onAccepted: PackController.exportPackTo("self", view.localPath(selectedFile))
    }

    FileDialog {
        id: importDialog
        title: qsTr("选择整合包")
        nameFilters: [qsTr("Minecraft 整合包 (*.mrpack *.zip)"), qsTr("所有文件 (*)")]
        onAccepted: PackController.importPack(view.localPath(selectedFile))
    }

    Flickable {
        anchors.fill: parent
        contentWidth: width
        contentHeight: page.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: page
            width: parent.width
            spacing: Tokens.spacingLg

            Item {
                width: parent.width
                height: Math.max(header.implicitHeight, actions.height)

                PageHeader {
                    id: header
                    width: parent.width - (actions.visible ? actions.width + Tokens.spacingLg : 0)
                    title: qsTr("出餐")
                    subtitle: qsTr("菜已备好；检查桌面，再选一个出口。")
                }

                Row {
                    id: actions
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.topMargin: 4
                    spacing: Tokens.spacingSm

                    AppButton {
                        text: qsTr("导入整合包…")
                        iconName: "import"
                        enabledByParent: !PackController.busy
                        onClicked: importDialog.open()
                    }
                    AppButton {
                        text: qsTr("重新准备")
                        iconName: "refresh"
                        visible: VersionDetail.state === "ready"
                        onClicked: view.prepareDish()
                    }
                }
            }

            Panel {
                width: parent.width
                height: Math.max(260, dishColumn.implicitHeight + padding * 2)
                visible: phase === 4

                Column {
                    id: dishColumn
                    width: parent.width
                    spacing: Tokens.spacingMd

                    Row {
                        width: parent.width
                        spacing: Tokens.spacingSm
                        SectionLabel { text: qsTr("这桌菜") }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: PackController.cooked ? qsTr("已烹饪") : qsTr("待烹饪")
                            color: PackController.cooked ? Tokens.primaryContainerInk : Tokens.warning
                            font.pixelSize: Tokens.fontSizeCaption
                            font.weight: Font.DemiBold
                        }
                    }

                    Row {
                        spacing: Tokens.spacingMd
                        Text {
                            text: PackController.packName
                            color: Tokens.text
                            font.pixelSize: Tokens.fontSizeTitle
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: PackController.versionId
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: view.servedLoader.length > 0
                            text: view.servedLoader
                            color: Tokens.primary
                            font.pixelSize: Tokens.fontSizeCaption
                            font.weight: Font.DemiBold
                        }
                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            visible: CookSession.loaderProfileId.length > 0
                            text: qsTr("profile %1").arg(CookSession.loaderProfileId)
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                            elide: Text.ElideMiddle
                        }
                    }

                    Row {
                        spacing: Tokens.spacingXl
                        Repeater {
                            model: [
                                { label: qsTr("文件"), value: String(PackController.fileCount) },
                                { label: qsTr("大小"), value: (PackController.totalBytes / 1048576).toFixed(0) + " MB" },
                                { label: qsTr("缺失"), value: String(PackController.missingFiles.length), danger: PackController.missingFiles.length > 0 },
                                { label: qsTr("冲突"), value: String(PackController.conflictCount) },
                                { label: qsTr("不可分发"), value: String(PackController.licenceDeniedGroups), danger: PackController.licenceDeniedGroups > 0 }
                            ]
                            delegate: Column {
                                spacing: 2
                                Text {
                                    text: modelData.label
                                    color: Tokens.textMuted
                                    font.pixelSize: Tokens.fontSizeCaption
                                }
                                Text {
                                    text: modelData.value
                                    color: !!modelData.danger ? Tokens.danger : Tokens.text
                                    font.pixelSize: Tokens.fontSizeTitle
                                    font.weight: Font.DemiBold
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 1
                        color: Tokens.hairline
                    }

                    Row {
                        width: parent.width
                        spacing: Tokens.spacingMd
                        Column {
                            width: parent.width - deployChange.width - Tokens.spacingMd
                            spacing: 2
                            Text {
                                text: qsTr("出菜目录")
                                color: Tokens.textMuted
                                font.pixelSize: Tokens.fontSizeCaption
                            }
                            Text {
                                width: parent.width
                                text: PackController.deployDir
                                color: Tokens.text
                                font.pixelSize: Tokens.fontSizeBody
                                elide: Text.ElideMiddle
                            }
                        }
                        AppButton {
                            id: deployChange
                            text: qsTr("更改")
                            iconName: "folder"
                            onClicked: folderDialog.open()
                        }
                    }

                    Text {
                        width: parent.width
                        text: view.blockedReason
                        visible: view.blockedReason.length > 0
                        color: Tokens.warning
                        font.pixelSize: Tokens.fontSizeCaption
                        elide: Text.ElideRight
                    }

                    Text {
                        width: parent.width
                        text: qsTr("上次出餐：%1").arg(PackController.lastOutput)
                        visible: PackController.lastOutput.length > 0
                        color: Tokens.textMuted
                        font.pixelSize: Tokens.fontSizeCaption
                        elide: Text.ElideMiddle
                    }

                    SectionLabel { text: qsTr("出口") }

                    Column {
                        width: parent.width
                        spacing: Tokens.spacingSm

                        Row {
                            width: parent.width
                            spacing: Tokens.spacingMd
                            Column {
                                width: parent.width - exitFolder.width - Tokens.spacingMd
                                spacing: 2
                                Text {
                                    text: qsTr("出菜到文件夹")
                                    color: Tokens.text
                                    font.pixelSize: Tokens.fontSizeBody
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    width: parent.width
                                    text: qsTr("把 mods/ 与配置写进游戏目录；覆盖前自动备份。")
                                    color: Tokens.textMuted
                                    font.pixelSize: Tokens.fontSizeCaption
                                    wrapMode: Text.WordWrap
                                }
                            }
                            AppButton {
                                id: exitFolder
                                text: qsTr("出菜")
                                iconName: "folder"
                                primary: true
                                enabledByParent: view.ready && view.loaderReady
                                onClicked: folderDialog.open()
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: Tokens.spacingMd
                            Column {
                                width: parent.width - exitMrpack.width - Tokens.spacingMd
                                spacing: 2
                                Text {
                                    text: qsTr("出菜到整合包")
                                    color: Tokens.text
                                    font.pixelSize: Tokens.fontSizeBody
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    width: parent.width
                                    text: qsTr("只打包清单与元数据（.mrpack），适合交给启动器。")
                                    color: Tokens.textMuted
                                    font.pixelSize: Tokens.fontSizeCaption
                                    wrapMode: Text.WordWrap
                                }
                            }
                            AppButton {
                                id: exitMrpack
                                text: qsTr("打包")
                                iconName: "export"
                                enabledByParent: view.ready
                                onClicked: mrpackDialog.open()
                            }
                        }

                        Row {
                            width: parent.width
                            spacing: Tokens.spacingMd
                            Column {
                                width: parent.width - exitSelf.width - Tokens.spacingMd
                                spacing: 2
                                Text {
                                    text: qsTr("自包含包")
                                    color: Tokens.text
                                    font.pixelSize: Tokens.fontSizeBody
                                    font.weight: Font.DemiBold
                                }
                                Text {
                                    width: parent.width
                                    text: qsTr("带上全部文件，离线可导入；受再分发许可约束。")
                                    color: Tokens.textMuted
                                    font.pixelSize: Tokens.fontSizeCaption
                                    wrapMode: Text.WordWrap
                                }
                            }
                            AppButton {
                                id: exitSelf
                                text: qsTr("打包")
                                iconName: "export"
                                enabledByParent: view.ready && !view.selfBlocked
                                onClicked: selfDialog.open()
                            }
                        }
                    }

                    Flickable {
                        id: licenceScroll
                        width: parent.width
                        height: Math.min(licenceColumn.height, Tokens.spacingXl * 11)
                        visible: PackController.licenceGroups.length > 0
                        clip: true
                        contentWidth: width
                        contentHeight: licenceColumn.height
                        boundsBehavior: Flickable.StopAtBounds

                        Column {
                            id: licenceColumn
                            width: parent.width
                            spacing: Tokens.spacingSm

                            Repeater {
                                model: PackController.licenceGroups
                                delegate: Row {
                                    width: parent.width
                                    spacing: Tokens.spacingSm

                                    Column {
                                        width: Math.max(120, parent.width - confirmLicence.width - parent.spacing)
                                        spacing: 2
                                        Text {
                                            width: parent.width
                                            text: view.licenceGroupLabel(modelData)
                                            color: modelData.verdict === "denied" ? Tokens.danger : Tokens.warning
                                            font.pixelSize: Tokens.fontSizeBody
                                            font.weight: Font.DemiBold
                                            elide: Text.ElideRight
                                        }
                                        Text {
                                            width: parent.width
                                            text: view.licenceGroupDetail(modelData)
                                            color: Tokens.textMuted
                                            font.pixelSize: Tokens.fontSizeCaption
                                            elide: Text.ElideRight
                                        }
                                    }

                                    AppButton {
                                        id: confirmLicence
                                        text: qsTr("已获授权")
                                        iconName: "check"
                                        onClicked: PackController.confirmGroup(modelData.id)
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Panel {
                width: parent.width
                height: Math.max(200, view.height - y - Tokens.spacingLg)
                visible: phase === 0
                Column {
                    width: parent.width
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Tokens.spacingMd

                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        name: "serve"
                        size: 28
                        color: Tokens.textMuted
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("还没有可出餐的内容")
                        color: Tokens.text
                        font.pixelSize: Tokens.fontSizeTitle
                        font.weight: Font.DemiBold
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(parent.width, 460)
                        text: VersionDetail.state === "ready"
                              ? qsTr("把 %1 的清单准备好，再出餐。").arg(VersionDetail.versionId)
                              : qsTr("先在「点菜」里挑一个版本，或导入一个整合包。")
                        color: Tokens.textMuted
                        font.pixelSize: Tokens.fontSizeBody
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                    }
                    Row {
                        anchors.horizontalCenter: parent.horizontalCenter
                        spacing: Tokens.spacingSm
                        AppButton {
                            text: VersionDetail.state === "ready" ? qsTr("生成出餐清单") : qsTr("去点菜")
                            iconName: VersionDetail.state === "ready" ? "check" : "versions"
                            primary: true
                            onClicked: {
                                if (VersionDetail.state === "ready")
                                    view.prepareDish();
                                else
                                    view.openOrder();
                            }
                        }
                        AppButton {
                            text: qsTr("导入整合包…")
                            iconName: "import"
                            enabledByParent: !PackController.busy
                            onClicked: importDialog.open()
                        }
                    }
                }
            }

            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Tokens.spacingMd
                visible: phase === 1 || phase === 3
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: phase === 1
                           ? (PackController.busy ? PackController.taskStep : qsTr("正在生成清单…"))
                           : PackController.error
                    color: phase === 3 ? Tokens.danger : Tokens.textMuted
                    font.pixelSize: Tokens.fontSizeBody
                }
                CircleProgress {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: 42
                    height: 42
                    indeterminate: phase === 1
                    value: phase === 1 ? (PackController.taskTotal > 0 ? PackController.taskDone / PackController.taskTotal : 0) : 1
                }
            }
        }
    }
}
