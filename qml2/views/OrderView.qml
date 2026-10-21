import QtQuick
import QtQuick.Controls.Basic
import BMCD
import BMCD.UI

// "点菜": pick the pot (version), then the ingredients (mods, 0.7).
// Five explicit states live in `state`: empty | loading | partial | error | success.
Item {
    id: view

    property int state: {
        if (VersionService.loading)
            return 1;                       // loading
        if (String(VersionService.error || "").length > 0)
            return 3;                       // error
        if (filtered.count === 0)
            return 0;                       // empty
        return 4;                           // success
    }
    readonly property var stateNames: [qsTr("空"), qsTr("读取中"), qsTr("部分"), qsTr("出错"), qsTr("就绪")]

    readonly property int selectedIndex: versionList.currentIndex
    readonly property string selectedId: {
        const item = versionList.itemAtIndex(versionList.currentIndex);
        return item && item.model ? item.model.id : "";
    }

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

    Row {
        anchors.fill: parent
        spacing: Tokens.spacingLg

        // ── version list ────────────────────────────────────────────────
        Column {
            width: Math.max(300, view.width * 0.38)
            height: parent.height
            spacing: Tokens.spacingMd

            Row {
                spacing: Tokens.spacingSm
                width: parent.width
                SearchField {
                    id: search
                    width: parent.width - channelRow.width - Tokens.spacingSm
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
                text: qsTr("%1 · %2 个版本").arg(view.stateNames[view.state]).arg(filtered.count)
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
                            text: row.type + (row.releaseTime ? " · " + Qt.formatDate(row.releaseTime, "yyyy-MM-dd") : "")
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
            width: parent.width - (Math.max(300, view.width * 0.38) + Tokens.spacingLg)
            height: parent.height
            padding: Tokens.spacingXl

            Column {
                width: parent.width
                spacing: Tokens.spacingLg
                visible: view.state === 4

                Text {
                    text: view.selectedId
                    color: Tokens.text
                    font.pixelSize: Tokens.fontSizeDisplay
                    font.weight: Font.DemiBold
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

                Row {
                    spacing: Tokens.spacingMd
                    AppButton {
                        text: VersionDetail.versionId === view.selectedId
                              && VersionDetail.state === "ready" ? qsTr("重新读取") : qsTr("读取清单")
                        onClicked: VersionDetail.select(view.selectedId, VersionService.urlFor(view.selectedId))
                    }
                    AppButton {
                        text: qsTr("下载（烹饪）")
                        primary: true
                        enabledByParent: VersionDetail.versionId === view.selectedId
                                         && VersionDetail.state === "ready"
                                         && VersionDetail.downloadState !== "running"
                        onClicked: VersionDetail.startDownload()
                    }
                    AppButton {
                        text: qsTr("去导出")
                        enabledByParent: VersionDetail.versionId === view.selectedId
                                         && VersionDetail.state === "ready"
                        onClicked: view.openServe()
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

            // loading / error / empty share one centred message block
            Column {
                anchors.centerIn: parent
                spacing: Tokens.spacingMd
                visible: view.state !== 4
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: view.state === 1 ? qsTr("正在读取版本清单…")
                        : view.state === 3 ? VersionService.error
                        : qsTr("没有匹配的版本")
                    color: view.state === 3 ? Tokens.danger : Tokens.textMuted
                    font.pixelSize: Tokens.fontSizeBody
                    wrapMode: Text.WordWrap
                    width: Math.min(view.width * 0.5, 420)
                    horizontalAlignment: Text.AlignHCenter
                }
                AppButton {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("刷新清单")
                    visible: view.state === 3 || view.state === 0
                    onClicked: VersionService.refresh()
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
