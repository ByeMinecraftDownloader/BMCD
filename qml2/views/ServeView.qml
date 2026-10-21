import QtQuick
import QtQuick.Dialogs
import BMCD
import BMCD.UI

// "上菜": the finished dish and its two equal exits. Serve is the authorisation
// point: preflight runs before anything is promised (space, conflicts, licences).
Item {
    id: view

    // `state` is a built-in Item property; use a distinct name so unqualified
    // bindings keep working inside this component.
    readonly property int phase: {
        if (PackController.state === "error")
            return 3;
        if (PackController.state === "loading" || PackController.state === "working")
            return 1;
        if (PackController.fileCount === 0)
            return 0;
        return 4;
    }

    readonly property bool ready: phase === 4 && !PackController.busy
    readonly property bool selfBlocked: PackController.licenceDenied > 0

    FolderDialog {
        id: folderDialog
        title: qsTr("选择导出目录")
        onAccepted: {
            PackController.serve();
        }
    }

    FileDialog {
        id: importDialog
        title: qsTr("选择整合包")
        nameFilters: [qsTr("Minecraft 整合包 (*.mrpack *.zip)"), qsTr("所有文件 (*)")]
        onAccepted: PackController.importPack(selectedFile)
    }

    Column {
        anchors.fill: parent
        spacing: Tokens.spacingLg

        Item {
            width: parent.width
            height: Math.max(header.implicitHeight, actions.height)

            PageHeader {
                id: header
                width: parent.width - actions.width - Tokens.spacingLg
                title: qsTr("导出")
                subtitle: qsTr("先检查冲突与许可，再选出口。")
            }

            Row {
                id: actions
                anchors.right: parent.right
                anchors.top: parent.top
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
                    onClicked: PackController.prepare(VersionDetail.versionId,
                                                      VersionService.urlFor(VersionDetail.versionId))
                }
            }
        }

        Panel {
            width: parent.width
            height: Math.max(200, packColumn.implicitHeight + padding * 2)
            visible: phase === 4

            Column {
                id: packColumn
                width: parent.width
                spacing: Tokens.spacingMd

                SectionLabel { text: qsTr("当前内容") }

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
                }

                Row {
                    spacing: Tokens.spacingXl
                    Repeater {
                        model: [
                            { label: qsTr("文件"), value: String(PackController.fileCount) },
                            { label: qsTr("大小"), value: (PackController.totalBytes / 1048576).toFixed(0) + " MB" },
                            { label: qsTr("冲突"), value: String(PackController.conflictCount) },
                            { label: qsTr("不可分发"), value: String(PackController.licenceDenied), danger: PackController.licenceDenied > 0 },
                            { label: qsTr("需确认"), value: String(PackController.licenceNeedsConfirm) }
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

                Text {
                    width: parent.width
                    visible: PackController.missingFiles.length > 0
                    text: qsTr("预检：%1").arg(PackController.missingFiles[0])
                    color: Tokens.danger
                    elide: Text.ElideRight
                    font.pixelSize: Tokens.fontSizeCaption
                }

                SectionLabel { text: qsTr("两个出口") }

                Row {
                    spacing: Tokens.spacingMd
                    AppButton {
                        text: qsTr("出菜到文件夹")
                        iconName: "folder"
                        primary: true
                        enabledByParent: view.ready
                        onClicked: PackController.serve()
                    }
                    AppButton {
                        text: qsTr("出菜到整合包（.mrpack）")
                        iconName: "export"
                        enabledByParent: view.ready
                        onClicked: PackController.exportPack("mrpack")
                    }
                    AppButton {
                        text: qsTr("自包含包")
                        iconName: "export"
                        enabledByParent: view.ready && !view.selfBlocked
                        onClicked: selfConfirm.open()
                    }
                }

                Row {
                    spacing: Tokens.spacingSm
                    visible: PackController.licenceDenied > 0 && PackController.deniedFiles.length > 0
                    Text {
                        text: qsTr("%1 未获得再分发许可").arg(PackController.deniedFiles[0])
                        color: Tokens.warning
                        font.pixelSize: Tokens.fontSizeCaption
                    }
                    AppButton {
                        text: qsTr("已获授权，继续")
                        iconName: "check"
                        onClicked: PackController.confirm(PackController.deniedFiles[0])
                    }
                }
            }
        }

        Panel {
            width: parent.width
            height: Math.max(200, parent.height - y)
            visible: phase === 0
            Column {
                id: emptyColumn
                width: parent.width
                anchors.verticalCenter: parent.verticalCenter
                spacing: Tokens.spacingMd
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: VersionDetail.state === "ready"
                          ? qsTr("准备把 %1 端出去？").arg(VersionDetail.versionId)
                          : qsTr("先在「版本」里挑一个版本。")
                    color: Tokens.textMuted
                    font.pixelSize: Tokens.fontSizeBody
                    horizontalAlignment: Text.AlignHCenter
                }
                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("生成导出清单")
                    iconName: "check"
                    primary: true
                    enabledByParent: VersionDetail.state === "ready"
                    onClicked: PackController.prepare(VersionDetail.versionId,
                                                      VersionService.urlFor(VersionDetail.versionId))
                }
                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("导入整合包（.mrpack / .zip）…")
                    iconName: "import"
                    enabledByParent: !PackController.busy
                    onClicked: importDialog.open()
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
                width: 48
                height: 48
                visible: phase === 1
                indeterminate: !PackController.busy || PackController.taskTotal === 0
                value: PackController.taskTotal > 0
                       ? PackController.taskDone / PackController.taskTotal : 0
            }
        }
    }

    ConfirmDialog {
        id: selfConfirm
        text: qsTr("自包含包会把 mod 本体一起打包再分发。请确认清单里每个文件的许可证都被允许；未标注许可证的文件默认不会被包含。")
        acceptText: qsTr("生成自包含包")
        onAccepted: PackController.exportPack("self")
    }
}
