import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import BMCD
import BMCD.UI

// "Pick your own mods": search Modrinth, tick projects, let the resolver pull
// required dependencies, then hand the resulting Pack to PackController.
Popup {
    id: picker

    property string gameVersion: ""
    property string activeLoader: CookSession.hasLoader ? CookSession.loaderType : "fabric"
    property var loaderChoices: [
        { key: "fabric", label: "Fabric" },
        { key: "neoforge", label: "NeoForge" },
        { key: "forge", label: "Forge" },
        { key: "quilt", label: "Quilt" }
    ]

    signal planAccepted(string json)

    function openFor(version) {
        gameVersion = version
        open()
    }

    modal: true
    closePolicy: Popup.CloseOnEscape
    padding: 0
    width: Math.min(980, (Overlay.overlay ? Overlay.overlay.width : 1120) - 64)
    height: Math.min(680, (Overlay.overlay ? Overlay.overlay.height : 760) - 48)
    anchors.centerIn: Overlay.overlay
    background: Rectangle {
        color: Tokens.surface
        radius: Tokens.radiusLg
        border.width: 1
        border.color: Tokens.hairline
    }

    onOpened: {
        ModsController.setContext(activeLoader, gameVersion)
        ModsController.search("")
        searchField.forceActiveFocus()
    }

    Connections {
        target: ModsController
        function onPlanReady(json) {
            picker.close()
            picker.planAccepted(json)
        }
    }

    Timer {
        id: searchTimer
        interval: 300
        onTriggered: ModsController.search(searchField.text)
    }

    Column {
        anchors.fill: parent
        anchors.margins: Tokens.spacingLg
        spacing: Tokens.spacingMd

        Row {
            width: parent.width
            spacing: Tokens.spacingMd

            Column {
                width: parent.width - closeButton.width - Tokens.spacingMd
                spacing: 2
                Text {
                    text: qsTr("添加模组")
                    color: Tokens.text
                    font.pixelSize: Tokens.fontSizeTitle
                    font.weight: Font.DemiBold
                }
                Text {
                    text: qsTr("搜索 Modrinth，勾选后自动解析必需依赖；下载到 mods/，加载器安装留给后续阶段。")
                    color: Tokens.textMuted
                    font.pixelSize: Tokens.fontSizeCaption
                    elide: Text.ElideRight
                    width: parent.width
                }
            }
            AppButton {
                id: closeButton
                text: qsTr("关闭")
                iconName: "close"
                onClicked: picker.close()
            }
        }

        Row {
            width: parent.width
            spacing: Tokens.spacingSm

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("加载器")
                color: Tokens.textMuted
                font.pixelSize: Tokens.fontSizeCaption
            }
            Repeater {
                model: picker.loaderChoices
                delegate: AppButton {
                    text: modelData.label
                    primary: picker.activeLoader === modelData.key
                    horizontalPadding: Tokens.spacingMd
                    onClicked: {
                        picker.activeLoader = modelData.key
                        CookSession.loaderType = modelData.key
                        CookSession.loaderVersion = ""
                        CookSession.refreshLoaderVersions()
                        ModsController.setContext(picker.activeLoader, picker.gameVersion)
                        ModsController.search(searchField.text)
                    }
                }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Minecraft %1").arg(picker.gameVersion)
                color: Tokens.textMuted
                font.pixelSize: Tokens.fontSizeCaption
            }
        }

        SearchField {
            id: searchField
            width: parent.width
            label: qsTr("搜索模组（名称、用途，例如 sodium / 优化）")
            onTextChanged: {
                if (text.trim().length >= 2)
                    searchTimer.restart()
                else {
                    searchTimer.stop()
                    if (text.trim().length === 0)
                        ModsController.search("")
                }
            }
            onAccepted: ModsController.search(text)
        }

        Row {
            width: parent.width
            height: parent.height - y - selectedRow.height - footer.height - Tokens.spacingMd * 2
            spacing: Tokens.spacingLg

            // search results
            Column {
                width: parent.width
                height: parent.height
                spacing: Tokens.spacingSm

                RowLayout {
                    width: parent.width
                    Text {
                        text: ModsController.searching ? qsTr("正在搜索…")
                              : qsTr("搜索结果 · %1").arg(ModsController.results.length)
                        color: Tokens.textMuted
                        font.pixelSize: Tokens.fontSizeCaption
                    }
                    Text {
                        Layout.fillWidth: true
                        text: ModsController.error
                        color: Tokens.danger
                        font.pixelSize: Tokens.fontSizeCaption
                        elide: Text.ElideRight
                        horizontalAlignment: Text.AlignRight
                    }
                }

                Item {
                    width: parent.width
                    height: parent.height - y

                ListView {
                    id: results
                    anchors.fill: parent
                    clip: true
                    spacing: 2
                    model: ModsController.results
                    boundsBehavior: Flickable.StopAtBounds

                    function maybeLoadMore() {
                        if (atYEnd && ModsController.hasMore && !ModsController.searching)
                            Qt.callLater(ModsController.loadMore);
                    }
                    onAtYEndChanged: maybeLoadMore()
                    onMovementEnded: maybeLoadMore()
                    onContentHeightChanged: maybeLoadMore()

                    delegate: Rectangle {
                        id: hit
                        required property var modelData
                        readonly property bool picked: ModsController.isSelected(modelData.slug)

                        width: ListView.view.width
                        height: 62
                        radius: Tokens.radiusSm
                        color: hover.hovered ? Tokens.surfaceHigh : "transparent"

                        HoverHandler { id: hover }

                        Row {
                            anchors.fill: parent
                            anchors.leftMargin: Tokens.spacingSm
                            anchors.rightMargin: Tokens.spacingSm
                            spacing: Tokens.spacingMd

                            Column {
                                width: parent.width - addButton.width - Tokens.spacingMd * 2
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 2
                                Text {
                                    width: parent.width
                                    text: hit.modelData.title
                                    color: Tokens.text
                                    font.pixelSize: Tokens.fontSizeBody
                                    font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                }
                                Text {
                                    width: parent.width
                                    text: hit.modelData.description
                                    color: Tokens.textMuted
                                    font.pixelSize: Tokens.fontSizeCaption
                                    elide: Text.ElideRight
                                }
                            }
                            AppButton {
                                id: addButton
                                anchors.verticalCenter: parent.verticalCenter
                                text: hit.picked ? qsTr("已选") : qsTr("加入")
                                iconName: hit.picked ? "check" : ""
                                enabledByParent: !hit.picked
                                onClicked: ModsController.addProject(hit.modelData.slug,
                                                                    hit.modelData.title,
                                                                    hit.modelData.iconUrl)
                            }
                        }
                    }

                }

                Rectangle {
                    anchors.fill: parent
                    radius: Tokens.radiusSm
                    color: Tokens.surface
                    opacity: 0.82
                    visible: ModsController.searching
                    BusyIndicator {
                        anchors.centerIn: parent
                        running: parent.visible
                    }
                }
                }

            }
            }

        Row {
            id: selectedRow
            width: parent.width
            height: selectedFlow.height

            Flow {
                id: selectedFlow
                width: parent.width
                spacing: Tokens.spacingSm
                Repeater {
                    model: ModsController.selected
                    delegate: Rectangle {
                        required property var modelData
                        width: chipText.width + Tokens.spacingMd * 2 + 18
                        height: 26
                        radius: 13
                        color: Tokens.surfaceAlt
                        border.width: 1
                        border.color: Tokens.outline
                        Text {
                            id: chipText
                            anchors.left: parent.left
                            anchors.leftMargin: Tokens.spacingMd
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.title
                            color: Tokens.text
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                        Text {
                            anchors.right: parent.right
                            anchors.rightMargin: Tokens.spacingSm
                            anchors.verticalCenter: parent.verticalCenter
                            text: "×"
                            color: Tokens.textMuted
                            font.pixelSize: 16
                        }
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: ModsController.removeProject(modelData.slug)
                        }
                    }
                }
            }
        }

        Row {
            id: footer
            width: parent.width
            spacing: Tokens.spacingMd

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: ModsController.planError.length > 0 ? ModsController.planError
                      : ModsController.awaitingConfirmation
                        ? qsTr("将自动加入 %1 个依赖：%2")
                              .arg(ModsController.awaitingDependencies.length)
                              .arg(ModsController.awaitingDependencies.join("、"))
                      : ModsController.note.length > 0 ? ModsController.note
                      : qsTr("已选 %1 个模组").arg(ModsController.selectedCount)
                color: ModsController.planError.length > 0 ? Tokens.danger : Tokens.textMuted
                font.pixelSize: Tokens.fontSizeCaption
                wrapMode: Text.WordWrap
                width: parent.width - planButton.width - cancelButton.width - Tokens.spacingMd * 3
            }
            AppButton {
                id: cancelButton
                text: qsTr("取消")
                onClicked: ModsController.awaitingConfirmation ? ModsController.cancelPlan() : picker.close()
            }
            AppButton {
                id: planButton
                text: ModsController.awaitingConfirmation
                      ? qsTr("确认加入")
                      : ModsController.planning ? qsTr("解析中…") : qsTr("生成备菜计划")
                iconName: ModsController.awaitingConfirmation ? "check" : "download"
                primary: true
                enabledByParent: ModsController.awaitingConfirmation
                                   || (ModsController.selectedCount > 0 && !ModsController.planning)
                onClicked: ModsController.awaitingConfirmation
                           ? ModsController.confirmPlan()
                           : ModsController.buildPlan()
            }
        }
    }

}
