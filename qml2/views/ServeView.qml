import QtQuick
import QtQuick.Dialogs
import BMCD
import BMCD.UI

// "上菜": the finished dish and its two equal exits. Serve is the authorisation
// point: preflight runs before anything is promised (space, conflicts, licences).
Item {
    id: view

    readonly property int state: {
        if (PackController.state === "error")
            return 3;
        if (PackController.state === "loading" || PackController.state === "working")
            return 1;
        if (PackController.fileCount === 0)
            return 0;
        return 4;
    }

    readonly property bool ready: state === 4 && !PackController.busy
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

        Row {
            width: parent.width
            Text {
                text: qsTr("导出")
                color: Tokens.text
                font.pixelSize: Tokens.fontSizeHeading
                font.weight: Font.DemiBold
            }
            Item { width: parent.width - 340; height: 1 }
            AppButton {
                text: qsTr("导入整合包…")
                enabledByParent: !PackController.busy
                onClicked: importDialog.open()
            }
            AppButton {
                text: qsTr("重新准备")
                visible: VersionDetail.state === "ready"
                onClicked: PackController.prepare(VersionDetail.versionId,
                                                  VersionService.urlFor(VersionDetail.versionId))
            }
        }

        Panel {
            width: parent.width
            height: 220
            visible: state === 4

            Column {
                anchors.fill: parent
                spacing: Tokens.spacingMd

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

                Row {
                    spacing: Tokens.spacingMd
                    AppButton {
                        text: qsTr("出菜到文件夹")
                        primary: true
                        enabledByParent: view.ready
                        onClicked: PackController.serve()
                    }
                    AppButton {
                        text: qsTr("出菜到整合包（.mrpack）")
                        enabledByParent: view.ready
                        onClicked: PackController.exportPack("mrpack")
                    }
                    AppButton {
                        text: qsTr("自包含包")
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
                        onClicked: PackController.confirm(PackController.deniedFiles[0])
                    }
                }
            }
        }

        Panel {
            width: parent.width
            height: 120
            visible: state === 0
            Column {
                anchors.centerIn: parent
                spacing: Tokens.spacingMd
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: VersionDetail.state === "ready"
                          ? qsTr("准备把 %1 端出去？").arg(VersionDetail.versionId)
                          : qsTr("先在「版本」里挑一个版本。")
                    color: Tokens.textMuted
                }
                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("生成导出清单")
                    primary: true
                    enabledByParent: VersionDetail.state === "ready"
                    onClicked: PackController.prepare(VersionDetail.versionId,
                                                      VersionService.urlFor(VersionDetail.versionId))
                }
                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("导入整合包（.mrpack / .zip）…")
                    enabledByParent: !PackController.busy
                    onClicked: importDialog.open()
                }
            }
        }

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Tokens.spacingMd
            visible: state === 1 || state === 3
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: state === 1
                       ? (PackController.busy ? PackController.taskStep : qsTr("正在生成清单…"))
                       : PackController.error
                color: state === 3 ? Tokens.danger : Tokens.textMuted
            }
            CircleProgress {
                anchors.horizontalCenter: parent.horizontalCenter
                width: 48
                height: 48
                visible: state === 1
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
