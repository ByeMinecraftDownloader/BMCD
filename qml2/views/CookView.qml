import QtQuick
import QtQuick.Controls.Basic
import BMCD
import BMCD.UI

// "烹饪": one serial job. CookSession owns game -> loader -> mods; this view
// only renders its step list and forwards explicit user actions.
Item {
    id: view

    readonly property bool hasPlan: CookSession.hasPlan
    readonly property bool running: CookSession.state === "running"
    readonly property bool paused: CookSession.state === "paused"
    readonly property bool finished: CookSession.state === "done"
    readonly property bool failed: CookSession.state === "error"

    signal openOrder()
    signal openServe()

    function stateLabel(state) {
        if (state === "running")
            return qsTr("进行中");
        if (state === "done")
            return qsTr("完成");
        if (state === "skipped")
            return qsTr("跳过");
        if (state === "error")
            return qsTr("失败");
        return qsTr("等待");
    }

    function stateColor(state) {
        if (state === "error")
            return Tokens.danger;
        if (state === "done")
            return Tokens.primary;
        if (state === "running")
            return Tokens.text;
        return Tokens.textMuted;
    }

    Column {
        anchors.fill: parent
        spacing: Tokens.spacingLg

        PageHeader {
            width: parent.width
            title: qsTr("烹饪")
            subtitle: qsTr("游戏本体、加载器、模组按顺序完成；失败时只重试失败的一步。")
        }

        Panel {
            width: parent.width
            height: Math.max(170, parent.height - y)
            visible: !view.hasPlan && !view.running && !view.failed && !view.finished

            Column {
                anchors.centerIn: parent
                spacing: Tokens.spacingMd
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    name: "queue"
                    size: 28
                    color: Tokens.textMuted
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("锅里还是空的")
                    color: Tokens.text
                    font.pixelSize: Tokens.fontSizeTitle
                    font.weight: Font.DemiBold
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("去「点菜」里挑一个锅底，读取清单后就能下锅。")
                    color: Tokens.textMuted
                    font.pixelSize: Tokens.fontSizeBody
                    horizontalAlignment: Text.AlignHCenter
                }
                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("去版本页")
                    iconName: "versions"
                    primary: true
                    onClicked: view.openOrder()
                }
            }
        }

        Panel {
            width: parent.width
            height: Math.max(120, planRow.implicitHeight + padding * 2)
            visible: view.hasPlan || view.running || view.failed || view.finished

            Row {
                id: planRow
                anchors.left: parent.left
                anchors.right: planActions.left
                anchors.rightMargin: Tokens.spacingLg
                anchors.verticalCenter: parent.verticalCenter
                spacing: Tokens.spacingLg

                CircleProgress {
                    id: planProgress
                    width: 56
                    height: 56
                    value: CookSession.progress
                    label: Math.round(CookSession.progress * 100) + "%"
                    indeterminate: view.running && CookSession.progress === 0
                }

                Column {
                    width: planRow.width - planProgress.width - planRow.spacing
                    spacing: 4
                    SectionLabel { text: qsTr("当前任务") }
                    Text {
                        width: parent.width
                        text: CookSession.planName
                        color: Tokens.text
                        font.pixelSize: Tokens.fontSizeTitle
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        elide: Text.ElideRight
                        text: !CookSession.loaderSelectedForPlan
                              ? (CookSession.requiredLoader.length > 0
                                 ? qsTr("需要 %1 加载器：回点菜页切换").arg(CookSession.requiredLoader)
                                 : qsTr("还没有选择加载器版本"))
                              : view.running
                              ? qsTr("正在进行：%1").arg(CookSession.currentStepTitle)
                              : view.paused ? qsTr("已暂停 · %1").arg(CookSession.currentStepTitle)
                              : view.finished ? qsTr("已完成，可以出餐了")
                              : view.failed ? qsTr("在「%1」失败").arg(CookSession.currentStepTitle)
                              : qsTr("等待开始")
                        color: !CookSession.loaderSelectedForPlan ? Tokens.warning
                               : view.failed ? Tokens.danger : Tokens.textMuted
                        font.pixelSize: Tokens.fontSizeCaption
                    }
                    Text {
                        width: parent.width
                        visible: CookSession.error.length > 0
                        text: CookSession.error
                        color: Tokens.danger
                        font.pixelSize: Tokens.fontSizeCaption
                        wrapMode: Text.WordWrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                    }
                }
            }

            Row {
                id: planActions
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                spacing: Tokens.spacingSm

                AppButton {
                    visible: view.running && CookSession.canPause
                    text: qsTr("暂停")
                    iconName: "pause"
                    onClicked: CookSession.pause()
                }
                AppButton {
                    visible: view.paused
                    text: qsTr("继续")
                    iconName: "play"
                    primary: true
                    onClicked: CookSession.resume()
                }
                AppButton {
                    visible: view.failed
                    text: qsTr("重试当前步")
                    iconName: "refresh"
                    primary: true
                    onClicked: CookSession.retry()
                }
                AppButton {
                    visible: view.running || view.paused
                    text: qsTr("取消")
                    iconName: "close"
                    danger: true
                    onClicked: {
                        CookSession.cancel();
                        view.openOrder();
                    }
                }
                AppButton {
                    visible: !view.running && !view.paused && !view.failed && !view.finished
                    text: qsTr("开始烹饪")
                    iconName: "download"
                    primary: true
                    enabledByParent: view.hasPlan && CookSession.loaderSelectedForPlan
                    onClicked: CookSession.start()
                }
                AppButton {
                    visible: view.finished
                    text: qsTr("去出餐")
                    iconName: "serve"
                    primary: true
                    onClicked: view.openServe()
                }
            }
        }

        Panel {
            id: stepsPanel
            width: parent.width
            height: Math.max(140, parent.height - y - (filePanel.visible
                         ? filePanel.height + Tokens.spacingLg : 0))
            visible: view.hasPlan

            Column {
                anchors.fill: parent
                spacing: Tokens.spacingSm

                SectionLabel { text: qsTr("烹饪步骤") }

                Repeater {
                    model: CookSession.steps

                    delegate: Column {
                        width: parent.width
                        spacing: 5

                        Row {
                            width: parent.width
                            spacing: Tokens.spacingSm

                            Icon {
                                anchors.verticalCenter: parent.verticalCenter
                                name: modelData.state === "done" || modelData.state === "skipped"
                                      ? "check" : modelData.state === "error" ? "warning" : "download"
                                size: 16
                                color: view.stateColor(modelData.state)
                            }
                            Text {
                                width: parent.width - status.width - parent.spacing * 2
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData.title
                                color: Tokens.text
                                font.pixelSize: Tokens.fontSizeBody
                                font.weight: modelData.state === "running" ? Font.DemiBold : Font.Normal
                                elide: Text.ElideRight
                            }
                            Text {
                                id: status
                                anchors.verticalCenter: parent.verticalCenter
                                text: view.stateLabel(modelData.state)
                                color: view.stateColor(modelData.state)
                                font.pixelSize: Tokens.fontSizeCaption
                            }
                        }

                        ProgressBar {
                            width: parent.width
                            value: modelData.progress
                            visible: modelData.state === "running" || modelData.state === "pending"
                                     || modelData.state === "error"
                        }

                        Text {
                            width: parent.width
                            visible: String(modelData.detail || "").length > 0
                            text: modelData.detail
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                            wrapMode: Text.WordWrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                        }
                    }
                }

                Row {
                    width: parent.width
                    spacing: Tokens.spacingSm
                    visible: CookSession.hasLoader
                             && CookSession.currentStep === "loader"
                             && CookSession.loaderLogPath.length > 0
                    Text {
                        width: parent.width - openLog.width - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("日志：%1").arg(CookSession.loaderLogPath)
                        color: Tokens.textMuted
                        font.pixelSize: Tokens.fontSizeCaption
                        elide: Text.ElideMiddle
                    }
                    AppButton {
                        id: openLog
                        text: qsTr("打开日志")
                        onClicked: Qt.openUrlExternally("file://" + CookSession.loaderLogPath)
                    }
                }
            }
        }

        Panel {
            id: filePanel
            width: parent.width
            height: 120
            visible: PackController.downloadFiles.length > 0

            Column {
                anchors.fill: parent
                spacing: Tokens.spacingSm

                SectionLabel { text: qsTr("模组文件进度") }

                ListView {
                    id: fileList
                    width: parent.width
                    height: parent.height - y
                    clip: true
                    spacing: 6
                    model: PackController.downloadFiles

                    delegate: Item {
                        width: fileList.width
                        height: 38

                        readonly property real ratio: modelData.total > 0
                            ? Math.min(1, modelData.done / modelData.total)
                            : (modelData.state === 2 ? 1 : 0)

                        Column {
                            width: parent.width
                            spacing: 4

                            Row {
                                width: parent.width
                                spacing: 8

                                Text {
                                    width: parent.width - percent.width - parent.spacing
                                    text: modelData.path
                                    color: modelData.state === 3 ? Tokens.danger : Tokens.text
                                    font.pixelSize: Tokens.fontSizeCaption
                                    elide: Text.ElideMiddle
                                }
                                Text {
                                    id: percent
                                    text: modelData.state === 2 ? qsTr("完成")
                                          : modelData.state === 3 ? qsTr("失败")
                                          : Math.round(ratio * 100) + "%"
                                    color: modelData.state === 3 ? Tokens.danger : Tokens.textMuted
                                    font.pixelSize: Tokens.fontSizeCaption
                                }
                            }

                            Rectangle {
                                width: parent.width
                                height: 3
                                radius: 1.5
                                color: Tokens.surfaceAlt

                                Rectangle {
                                    width: parent.width * ratio
                                    height: parent.height
                                    radius: parent.radius
                                    color: modelData.state === 3 ? Tokens.danger : Tokens.primary
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    onHasPlanChanged: CookSession.prepare()
    Component.onCompleted: CookSession.prepare()
}
