import QtQuick
import QtQuick.Controls.Basic
import BMCD
import BMCD.UI

// "点菜": pick the pot (version), then the ingredients (mods, 0.7).
// Five explicit states live in `phase`: empty | loading | partial | error | success.
Item {
    id: view

    readonly property int phase: {
        if (VersionService.loading)
            return 1;                       // loading
        if (String(VersionService.error || "").length > 0)
            return 3;                       // error
        if (filtered.count === 0)
            return 0;                       // empty
        return 4;                           // success
    }
    readonly property var stateNames: [qsTr("空"), qsTr("读取中"), qsTr("部分"), qsTr("出错"), qsTr("就绪")]

    readonly property int listWidth: Math.min(460, Math.max(300, width * 0.38))
    readonly property int selectedIndex: versionList.currentIndex
    readonly property string selectedId: versionList.currentItem
                                         ? versionList.currentItem.versionId : ""

    signal openServe()

    VersionFilterModel {
        id: filtered
        sourceModel: VersionService.versions
    }

    property var suggestions: []

    // Local knowledge index: soft hints under the search field, never blocking.
    Timer {
        id: ragTimer
        interval: 260
        onTriggered: view.suggestions = RagController.available
                     ? RagController.suggest(search.text, 3)
                     : []
    }

    Column {
        anchors.fill: parent
        spacing: Tokens.spacingLg

        PageHeader {
            width: parent.width
            title: qsTr("版本")
            subtitle: qsTr("挑一个锅底；读取清单后就能下锅。")
        }

        Row {
            width: parent.width
            height: parent.height - y
            spacing: Tokens.spacingLg

        // ── version list ────────────────────────────────────────────────
        Column {
            width: view.listWidth
            height: parent.height
            spacing: Tokens.spacingMd

            SearchField {
                id: search
                width: parent.width
                label: qsTr("搜索版本（/ 聚焦）")
                onTextChanged: {
                    filtered.searchText = text;
                    if (text.length >= 2)
                        ragTimer.restart();
                    else {
                        ragTimer.stop();
                        view.suggestions = [];
                    }
                }
            }

            Row {
                id: channelRow
                spacing: 2
                Repeater {
                    model: [{ key: "release", label: qsTr("正式") },
                            { key: "snapshot", label: qsTr("快照") },
                            { key: "all", label: qsTr("全部") }]
                    delegate: AppButton {
                        text: modelData.label
                        primary: filtered.channel === modelData.key
                        horizontalPadding: Tokens.spacingMd
                        onClicked: filtered.channel = modelData.key
                    }
                }
            }

            Column {
                width: parent.width
                spacing: 2
                visible: view.suggestions.length > 0
                Repeater {
                    model: view.suggestions
                    delegate: Text {
                        width: parent.width
                        text: qsTr("相关：%1").arg(modelData)
                        color: Tokens.textMuted
                        elide: Text.ElideRight
                        font.pixelSize: Tokens.fontSizeCaption
                    }
                }
            }

            Text {
                width: parent.width
                text: qsTr("%1 · %2 个版本").arg(view.stateNames[view.phase]).arg(filtered.count)
                color: Tokens.textMuted
                font.pixelSize: Tokens.fontSizeCaption
            }

            ListView {
                id: versionList
                width: parent.width
                height: parent.height - y
                clip: true
                spacing: Tokens.spacingSm
                model: filtered
                currentIndex: 0
                boundsBehavior: Flickable.StopAtBounds

                delegate: Panel {
                    id: row
                    readonly property string versionId: model.id

                    width: ListView.view.width
                    interactive: true
                    selected: ListView.isCurrentItem
                    padding: Tokens.spacingMd
                    height: 58

                    Column {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            text: row.versionId
                            color: Tokens.text
                            font.pixelSize: Tokens.fontSizeBody
                            font.weight: ListView.isCurrentItem ? Font.DemiBold : Font.Normal
                        }
                        Text {
                            text: (model.type === "release" ? qsTr("正式")
                                                : model.type === "snapshot" ? qsTr("快照")
                                                                            : model.type)
                              + (model.releaseTime ? " · " + Qt.formatDate(model.releaseTime, "yyyy-MM-dd") : "")
                                  + (InstalledVersions.contains(row.versionId) ? qsTr(" · 已安装") : "")
                            color: Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                        }
                    }

                    AppButton {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        text: ListView.isCurrentItem ? qsTr("已选") : qsTr("选择")
                        primary: ListView.isCurrentItem
                        onClicked: {
                            versionList.currentIndex = index;
                            versionList.positionViewAtIndex(index, ListView.Contain);
                        }
                    }

                    TapHandler { onTapped: versionList.currentIndex = index }
                }

                // stamp moment: pressing a card pushes it in and lets go
                onCurrentIndexChanged: {
                    if (currentItem)
                        currentItem.scale = 1.0;
                }
            }
        }

        // ── detail / list panel ─────────────────────────────────────────
        Panel {
            width: parent.width - (view.listWidth + Tokens.spacingLg)
            height: parent.height
            padding: Tokens.spacingXl

            Column {
                width: parent.width
                spacing: Tokens.spacingLg
                visible: view.phase === 4

                SectionLabel { text: qsTr("版本详情") }

                Text {
                    width: parent.width
                    text: view.selectedId
                    color: Tokens.text
                    font.pixelSize: Tokens.fontSizeDisplay
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }
                Text {
                    width: parent.width
                    text: qsTr("放进清单，下一步是把它端到哪里去。")
                    color: Tokens.textVariant
                    font.pixelSize: Tokens.fontSizeBody
                }

                Row {
                    spacing: Tokens.spacingXl
                    Repeater {
                        model: [
                            { label: qsTr("文件"), value: VersionDetail.versionId === view.selectedId
                                              ? String(VersionDetail.fileCount) : "—" },
                            { label: qsTr("大小"), value: VersionDetail.versionId === view.selectedId
                                            ? (VersionDetail.totalBytes / 1048576).toFixed(0) + " MB" : "—" },
                            { label: qsTr("Java"), value: VersionDetail.versionId === view.selectedId
                                            ? (VersionDetail.javaMajor > 0 ? String(VersionDetail.javaMajor) : "—") : "—" },
                            { label: qsTr("素材"), value: VersionDetail.versionId === view.selectedId
                                            ? String(VersionDetail.assetCount) : "—" }
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
                                color: Tokens.text
                                font.pixelSize: Tokens.fontSizeTitle
                                font.weight: Font.DemiBold
                            }
                        }
                    }
                }

                Text {
                    width: parent.width
                    visible: VersionDetail.error.length > 0 && VersionDetail.versionId === view.selectedId
                    text: VersionDetail.error
                    color: Tokens.danger
                    wrapMode: Text.WordWrap
                    font.pixelSize: Tokens.fontSizeCaption
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: detailActions.top
                anchors.bottomMargin: Tokens.spacingLg
                height: 1
                color: Tokens.outline
                visible: view.phase === 4
            }

            Flow {
                id: detailActions
                width: parent.width
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                spacing: Tokens.spacingMd
                visible: view.phase === 4

                AppButton {
                    text: VersionDetail.versionId === view.selectedId
                          && VersionDetail.state === "ready" ? qsTr("重新读取") : qsTr("读取清单")
                    onClicked: VersionDetail.select(view.selectedId, VersionService.urlFor(view.selectedId))
                }
                AppButton {
                    text: qsTr("下载（烹饪）")
                    iconName: "download"
                    primary: true
                    enabledByParent: VersionDetail.versionId === view.selectedId
                                     && VersionDetail.state === "ready"
                                     && VersionDetail.downloadState !== "running"
                    onClicked: VersionDetail.startDownload()
                }
                AppButton {
                    text: qsTr("去导出")
                    iconName: "serve"
                    enabledByParent: VersionDetail.versionId === view.selectedId
                                     && VersionDetail.state === "ready"
                    onClicked: view.openServe()
                }
            }

            // loading / error / empty share one centred message block
            Column {
                anchors.centerIn: parent
                spacing: Tokens.spacingMd
                visible: view.phase !== 4
                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: view.phase !== 1
                    name: view.phase === 3 ? "warning" : "search"
                    size: 22
                    color: view.phase === 3 ? Tokens.danger : Tokens.textMuted
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: view.phase === 1 ? qsTr("正在读取版本清单…")
                        : view.phase === 3 ? VersionService.error
                        : qsTr("没有匹配的版本")
                    color: view.phase === 3 ? Tokens.danger : Tokens.textMuted
                    font.pixelSize: Tokens.fontSizeBody
                    wrapMode: Text.WordWrap
                    width: Math.min(view.width * 0.5, 420)
                    horizontalAlignment: Text.AlignHCenter
                }
                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("刷新清单")
                    iconName: "refresh"
                    visible: view.phase === 3 || view.phase === 0
                    onClicked: VersionService.refresh()
                }
            }
        }
        }
    }

    RippleCanvas {
        id: stampRipple
        width: 140
        height: 140
        anchors.right: parent.right
        anchors.rightMargin: 40
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 40
        visible: running
    }

    Component.onCompleted: {
        if (VersionService.versions.rowCount() === 0)
            VersionService.refresh();
    }
}
