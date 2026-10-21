import QtQuick
import QtQuick.Controls.Basic
import BMCD as Bmcd
import BMCD.UI

Item {
    id: view

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
                subtitle: qsTr("路径、语言和本地索引。")
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

                    SettingRow { label: qsTr("游戏目录"); value: VersionDetail.targetDir }
                    SettingRow { label: qsTr("版本"); value: Qt.application.version }
                    SettingRow { label: qsTr("语言"); value: uiLanguage }
                    SettingRow { label: qsTr("主题来源")
                                 value: qsTr("%1（%2）").arg(Bmcd.Palette.tokenMode).arg(Bmcd.Palette.schemeName) }
                    SettingRow { label: qsTr("减少动效")
                                 value: Bmcd.Palette.reduceMotion ? qsTr("跟随系统：开") : qsTr("跟随系统：关") }

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
                            onClicked: InstalledVersions.rescan()
                        }
                    }
                }
            }

            Panel {
                id: ragPanel
                width: parent.width
                height: ragColumn.implicitHeight + padding * 2

                Column {
                    id: ragColumn
                    width: parent.width
                    spacing: Tokens.spacingSm

                    SectionLabel { text: qsTr("检索索引") }

                    SettingRow {
                        label: qsTr("状态")
                        labelWidth: 52
                        value: RagController.available
                               ? qsTr("%1 个片段 · %2").arg(RagController.chunkCount).arg(RagController.embedderName)
                               : qsTr("未建立（bmcd --rag-build）")
                    }
                    SettingRow { label: qsTr("位置"); labelWidth: 52; value: RagController.stats().indexPath }
                    SettingRow { label: qsTr("说明"); labelWidth: 52
                                 value: RagController.lastNote || qsTr("本地索引，不联网") }
                }
            }
        }
    }
}
