import QtQuick
import BMCD
import BMCD.UI

// "烹饪": the global queue. Collapsed into a 2 px line at the bottom of the
// window while downloads run; the full view is one nav click away.
Item {
    id: view

    readonly property bool hasPlan: VersionDetail.state === "ready"
    readonly property string downloadState: VersionDetail.downloadState
    readonly property bool running: downloadState === "running" || downloadState === "paused"
    readonly property bool failed: downloadState === "failed" || VersionDetail.downloadError.length > 0
    readonly property bool finished: downloadState === "done"
    readonly property int total: Math.max(1, VersionDetail.downloadTotalFiles)
    readonly property real progress: VersionDetail.downloadTotalBytes > 0
        ? VersionDetail.doneBytes / VersionDetail.downloadTotalBytes
        : (VersionDetail.downloadTotalFiles > 0
           ? VersionDetail.doneFiles / VersionDetail.downloadTotalFiles : 0)

    Column {
        anchors.fill: parent
        spacing: Tokens.spacingLg

        Text {
            text: qsTr("队列")
            color: Tokens.text
            font.pixelSize: Tokens.fontSizeHeading
            font.weight: Font.DemiBold
        }

        Panel {
            width: parent.width
            height: 96
            visible: !view.hasPlan && !view.running
            Text {
                anchors.centerIn: parent
                text: qsTr("锅里还是空的：去「版本」里挑一个锅底。")
                color: Tokens.textMuted
            }
        }

        Panel {
            width: parent.width
            height: 132
            visible: view.hasPlan || view.running || view.finished

            Row {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Tokens.spacingLg

                CircleProgress {
                    width: 56
                    height: 56
                    value: view.progress
                    label: Math.round(view.progress * 100) + "%"
                    indeterminate: view.running && view.progress === 0
                }

                Column {
                    spacing: 4
                    Text {
                        text: VersionDetail.versionId
                        color: Tokens.text
                        font.pixelSize: Tokens.fontSizeTitle
                        font.weight: Font.DemiBold
                    }
                    Text {
                        text: view.running
                              ? qsTr("%1 / %2 文件 · %3 / %4 MB · %5 MB/s")
                                    .arg(VersionDetail.doneFiles).arg(VersionDetail.downloadTotalFiles)
                                    .arg((VersionDetail.doneBytes / 1048576).toFixed(1))
                                    .arg((VersionDetail.downloadTotalBytes / 1048576).toFixed(0))
                                    .arg(VersionDetail.speed.toFixed(1))
                              : view.finished ? qsTr("已完成") : view.failed ? qsTr("已中断") : qsTr("等待开始")
                        color: view.failed ? Tokens.danger : Tokens.textMuted
                        font.pixelSize: Tokens.fontSizeCaption
                    }
                    Text {
                        visible: VersionDetail.downloadError.length > 0
                        text: VersionDetail.downloadError
                        color: Tokens.danger
                        font.pixelSize: Tokens.fontSizeCaption
                        elide: Text.ElideRight
                        width: Math.max(160, view.width - 220)
                    }
                }
            }

            Row {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Tokens.spacingSm

                AppButton {
                    visible: view.downloadState === "running"
                    text: qsTr("暂停")
                    onClicked: VersionDetail.pauseDownload()
                }
                AppButton {
                    visible: view.downloadState === "paused"
                    text: qsTr("继续")
                    primary: true
                    onClicked: VersionDetail.startDownload()
                }
                AppButton {
                    visible: view.running
                    text: qsTr("取消")
                    danger: true
                    onClicked: VersionDetail.cancelDownload()
                }
                AppButton {
                    visible: !view.running
                    text: view.finished ? qsTr("重新校验") : qsTr("开始")
                    primary: true
                    enabledByParent: view.hasPlan
                    onClicked: VersionDetail.startDownload()
                }
            }
        }
    }
}
