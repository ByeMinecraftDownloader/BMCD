import QtQuick
import BMCD
import BMCD.UI

Item {
    id: view

    Column {
        anchors.fill: parent
        spacing: Tokens.spacingLg

        Text {
            text: qsTr("设置")
            color: Tokens.text
            font.pixelSize: Tokens.fontSizeHeading
            font.weight: Font.DemiBold
        }

        Panel {
            width: parent.width
            height: 180
            Column {
                anchors.fill: parent
                spacing: Tokens.spacingMd

                Repeater {
                    model: [
                        { label: qsTr("游戏目录"), value: VersionDetail.targetDir },
                        { label: qsTr("版本"), value: Qt.application.version },
                        { label: qsTr("语言"), value: uiLanguage },
                        { label: qsTr("主题来源"), value: qsTr("%1（%2）").arg(Palette.tokenMode).arg(Palette.schemeName) },
                        { label: qsTr("减少动效"), value: Palette.reduceMotion ? qsTr("跟随系统：开") : qsTr("跟随系统：关") }
                    ]
                    delegate: Row {
                        spacing: Tokens.spacingMd
                        Text {
                            width: 90
                            text: modelData.label
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                        Text {
                            text: modelData.value
                            color: Tokens.text
                            font.pixelSize: Tokens.fontSizeBody
                            elide: Text.ElideMiddle
                            width: view.width - 200
                        }
                    }
                }

                Row {
                    spacing: Tokens.spacingMd
                    AppButton {
                        text: qsTr("刷新版本清单")
                        onClicked: VersionService.refresh()
                    }
                    AppButton {
                        text: qsTr("重新扫描已安装")
                        onClicked: InstalledVersions.rescan()
                    }
                }
            }
        }

        Panel {
            width: parent.width
            height: 132
            Column {
                anchors.fill: parent
                spacing: Tokens.spacingSm

                Text {
                    text: qsTr("检索索引")
                    color: Tokens.text
                    font.pixelSize: Tokens.fontSizeBody
                    font.weight: Font.DemiBold
                }
                Repeater {
                    model: [
                        { label: qsTr("状态"), value: RagController.available
                              ? qsTr("%1 个片段 · %2").arg(RagController.chunkCount).arg(RagController.embedderName)
                              : qsTr("未建立（bmcd --rag-build）") },
                        { label: qsTr("位置"), value: RagController.stats().indexPath },
                        { label: qsTr("说明"), value: RagController.lastNote || qsTr("本地索引，不联网") }
                    ]
                    delegate: Row {
                        spacing: Tokens.spacingMd
                        Text {
                            width: 48
                            text: modelData.label
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                        Text {
                            width: view.width - 140
                            text: modelData.value
                            color: Tokens.text
                            font.pixelSize: Tokens.fontSizeCaption
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }
        }
    }
}
