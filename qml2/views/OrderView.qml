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
    property string planFailure: ""
    property string planHint: ""
    property bool initialSelectionDone: false

    function formatBytes(n) {
        if (n < 1024) return n + " B";
        if (n < 1024 * 1024) return (n / 1024).toFixed(1) + " KB";
        if (n < 1024 * 1024 * 1024) return (n / (1024 * 1024)).toFixed(1) + " MB";
        return (n / (1024 * 1024 * 1024)).toFixed(2) + " GB";
    }

    readonly property var stateNames: [qsTr("空"), qsTr("读取中"), qsTr("部分"), qsTr("出错"), qsTr("就绪")]

    readonly property int listWidth: Math.min(460, Math.max(300, width * 0.38))
    readonly property int selectedIndex: versionList.currentIndex
    readonly property string selectedId: versionList.currentItem
                                         ? versionList.currentItem.versionId : ""
    readonly property bool planLoaded: VersionDetail.versionId === selectedId
                                       && VersionDetail.state === "ready"
    readonly property bool planLoading: VersionDetail.state === "loading"

    function selectInitialVersion() {
        if (initialSelectionDone || typeof initialVersion === "undefined"
                || initialVersion.length === 0)
            return;
        filtered.channel = "all";
        const index = filtered.indexOfVersion(initialVersion);
        if (index < 0)
            return;
        initialSelectionDone = true;
        versionList.currentIndex = index;
        versionList.positionViewAtIndex(index, ListView.Contain);
        if (typeof autoStartDownload !== "undefined" && autoStartDownload)
            VersionDetail.select(initialVersion, VersionService.urlFor(initialVersion));
    }

    Connections {
        target: filtered
        function onCountChanged() { view.selectInitialVersion(); }
    }

    onSelectedIdChanged: {
        planFailure = "";
        planHint = "";
        if (CookSession.hasLoader) {
            CookSession.loaderVersion = "";
            CookSession.refreshLoaderVersions();
        }
        ModsController.setContext(CookSession.hasLoader ? CookSession.loaderType : "fabric", selectedId);
    }

    Connections {
        target: ModsController
        function onSelectedChanged() {
            view.planHint = "";
        }
    }

    signal openServe()
    signal openCook()
    signal openSettings()

    Connections {
        target: CookSession
        function onChanged() {
            if (CookSession.hasLoader)
                ModsController.setContext(CookSession.loaderType, view.selectedId);
        }
    }

    ModPicker {
        id: modPicker
        gameVersion: view.selectedId
        onPlanAccepted: function(json) {
            view.planFailure = "";
            if (!PackController.adoptPack(json)) {
                view.planFailure = PackController.error;
                return;
            }
            view.planHint = qsTr("备菜完成 · %1 个文件 · %2，点“去烹饪”确认下载")
                            .arg(PackController.fileCount)
                            .arg(view.formatBytes(PackController.totalBytes));
            if (!CookSession.loaderSelectedForPlan && CookSession.requiredLoader.length > 0)
                view.planHint = qsTr("这些配菜需要 %1 加载器：请在“加载器”里切到 %1。")
                                .arg(CookSession.requiredLoader);
        }
    }

    VersionFilterModel {
        id: filtered
        sourceModel: VersionService.versions
    }

    Column {
        anchors.fill: parent
        spacing: Tokens.spacingLg

        PageHeader {
            width: parent.width
            title: qsTr("点菜")
            subtitle: qsTr("先选锅底，再按需加配菜；菜篮满了就开火。")
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
                label: qsTr("搜索版本")
                onTextChanged: filtered.searchText = text
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
                    readonly property bool isCurrent: ListView.isCurrentItem

                    width: ListView.view.width
                    interactive: true
                    selected: row.isCurrent
                    padding: Tokens.spacingMd
                    height: 58
                    Accessible.role: Accessible.ListItem
                    Accessible.name: row.versionId
                    Accessible.selected: row.isCurrent

                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: 6
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: 26
                        radius: 1.5
                        color: Tokens.primary
                        visible: row.isCurrent
                    }

                    Column {
                        width: parent.width - 30
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 3
                        Text {
                            text: row.versionId
                            color: Tokens.text
                            font.pixelSize: Tokens.fontSizeBody
                            font.weight: row.isCurrent ? Font.DemiBold : Font.Normal
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

                    Icon {
                        anchors.right: parent.right
                        anchors.rightMargin: 2
                        anchors.verticalCenter: parent.verticalCenter
                        name: "check"
                        size: 16
                        color: Tokens.primary
                        visible: row.isCurrent
                    }

                    TapHandler {
                        cursorShape: Qt.PointingHandCursor
                        onTapped: versionList.currentIndex = index
                    }
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
                // Without a plan this is the only content in the panel, so centre
                // it; once the manifest is loaded the heading belongs at the top.
                y: view.planLoaded ? 0 : Math.max(0, (parent.height - implicitHeight) / 2)

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
                    text: view.planLoaded
                          ? qsTr("放进清单，下一步是把它端到哪里去。")
                          : qsTr("先读取这个版本的清单。")
                    color: Tokens.textVariant
                    font.pixelSize: Tokens.fontSizeBody
                }

                Row {
                    visible: view.planLoaded
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

                Column {
                    width: parent.width
                    spacing: Tokens.spacingSm
                    visible: view.planLoaded

                    SectionLabel { text: qsTr("加载器") }

                    Row {
                        width: parent.width
                        spacing: Tokens.spacingSm

                        ComboBox {
                            id: loaderTypeBox
                            width: 132
                            model: [
                                { key: "vanilla", label: qsTr("原版") },
                                { key: "fabric", label: "Fabric" },
                                { key: "quilt", label: "Quilt" },
                                { key: "forge", label: "Forge" },
                                { key: "neoforge", label: "NeoForge" }
                            ]
                            textRole: "label"
                            currentIndex: {
                                for (let i = 0; i < count; ++i) {
                                    if (model[i].key === CookSession.loaderType)
                                        return i;
                                }
                                return 0;
                            }
                            onActivated: CookSession.loaderType = model[currentIndex].key
                        }

                        TextField {
                            id: loaderVersionField
                            width: Math.max(110, parent.width - loaderTypeBox.width
                                            - loaderVersionBox.width - Tokens.spacingSm * 2)
                            text: CookSession.loaderVersion
                            placeholderText: qsTr("版本，可手填")
                            enabled: CookSession.hasLoader
                            onEditingFinished: CookSession.loaderVersion = text.trim()
                        }

                        ComboBox {
                            id: loaderVersionBox
                            width: 126
                            model: CookSession.loaderVersions
                            textRole: "label"
                            enabled: CookSession.hasLoader && CookSession.loaderVersions.length > 0
                                     && !CookSession.loaderVersionsLoading
                            displayText: CookSession.loaderVersionsLoading ? qsTr("读取中…")
                                         : CookSession.loaderVersions.length > 0 ? qsTr("推荐")
                                                                                 : qsTr("无推荐")
                            onActivated: {
                                if (index >= 0 && index < CookSession.loaderVersions.length)
                                    CookSession.loaderVersion = CookSession.loaderVersions[index].version;
                            }
                        }
                    }

                    Text {
                        width: parent.width
                        visible: CookSession.requiredLoader.length > 0
                                 && !CookSession.loaderSelectedForPlan
                        text: qsTr("这个整合包需要 %1 加载器；请在上方切到 %1 再烹饪。")
                              .arg(CookSession.requiredLoader)
                        color: Tokens.warning
                        font.pixelSize: Tokens.fontSizeCaption
                        wrapMode: Text.WordWrap
                    }

                    Row {
                        width: parent.width
                        spacing: Tokens.spacingSm
                        Text {
                            width: parent.width - javaAction.width - Tokens.spacingSm
                            text: CookSession.hasLoader
                                  ? (CookSession.javaReady
                                     ? qsTr("Java %1 · 将写入 %2").arg(CookSession.javaVersion)
                                       .arg(CookSession.loaderProfileName)
                                     : qsTr("未检测到 Java；安装加载器前请先在设置页选择 Java。"))
                                  : qsTr("原版不需要安装加载器，模组步骤也会跳过。")
                            color: CookSession.hasLoader && !CookSession.javaReady
                                   ? Tokens.danger : Tokens.textMuted
                            font.pixelSize: Tokens.fontSizeCaption
                            wrapMode: Text.WordWrap
                        }
                        AppButton {
                            id: javaAction
                            text: qsTr("去设置")
                            visible: CookSession.hasLoader && !CookSession.javaReady
                            onClicked: view.openSettings()
                        }
                    }
                    Text {
                        width: parent.width
                        visible: CookSession.loaderVersionsError.length > 0 && CookSession.hasLoader
                        text: CookSession.loaderVersionsError + qsTr("（可手填版本）")
                        color: Tokens.danger
                        font.pixelSize: Tokens.fontSizeCaption
                        wrapMode: Text.WordWrap
                    }
                }

                Column {
                    width: parent.width
                    visible: !view.planLoaded
                    spacing: Tokens.spacingMd

                    Text {
                        width: parent.width
                        text: view.planLoading
                              ? qsTr("正在读取版本清单…")
                              : qsTr("读取清单后，这里会显示文件数、大小、Java 与素材统计。")
                        color: view.planLoading ? Tokens.textVariant : Tokens.textMuted
                        wrapMode: Text.WordWrap
                        font.pixelSize: Tokens.fontSizeCaption
                    }
                    Text {
                        width: parent.width
                        visible: VersionDetail.error.length > 0
                        text: VersionDetail.error
                        color: Tokens.danger
                        wrapMode: Text.WordWrap
                        font.pixelSize: Tokens.fontSizeCaption
                    }
                    AppButton {
                        text: view.planLoading ? qsTr("正在读取…") : qsTr("读取清单")
                        iconName: "refresh"
                        primary: true
                        enabledByParent: !view.planLoading
                        onClicked: VersionDetail.select(view.selectedId,
                                                        VersionService.urlFor(view.selectedId))
                    }
                }
            }

            // Basket: the only thing "add mods" changes until the user asks for
            // a plan. Keeping it above the divider leaves the bottom actions
            // stable at every window size.
            Row {
                width: parent.width
                height: 24
                spacing: Tokens.spacingMd
                visible: view.planLoaded && ModsController.selectedCount > 0

                SectionLabel { text: qsTr("菜篮 · %1").arg(ModsController.selectedCount) }
                Text {
                    width: parent.width - 150
                    anchors.verticalCenter: parent.verticalCenter
                    text: ModsController.selected.map(function(entry) { return entry.title }).join("、")
                    color: Tokens.textMuted
                    font.pixelSize: Tokens.fontSizeCaption
                    elide: Text.ElideRight
                }
                AppButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("管理")
                    onClicked: modPicker.openFor(view.selectedId)
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: detailActions.top
                anchors.bottomMargin: view.planFailure.length > 0 ? 38 : Tokens.spacingLg
                height: 1
                color: Tokens.hairline
                visible: view.phase === 4 && view.planLoaded
            }

            Column {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: detailActions.top
                anchors.bottomMargin: 8
                spacing: 4

                Text {
                    width: parent.width
                    text: view.planFailure
                    color: Tokens.danger
                    font.pixelSize: Tokens.fontSizeCaption
                    elide: Text.ElideRight
                    visible: view.planFailure.length > 0
                }
                Text {
                    width: parent.width
                    text: view.planHint
                    color: Tokens.primary
                    font.pixelSize: Tokens.fontSizeCaption
                    elide: Text.ElideRight
                    visible: view.planHint.length > 0
                }
            }

            Flow {
                id: detailActions
                width: parent.width
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                spacing: Tokens.spacingMd
                visible: view.phase === 4 && view.planLoaded

                AppButton {
                    text: qsTr("去烹饪")
                    iconName: "download"
                    primary: true
                    enabledByParent: CookSession.hasPlan && CookSession.loaderSelectedForPlan
                    onClicked: view.openCook()
                }
                AppButton {
                    text: qsTr("添加配菜")
                    iconName: "queue"
                    visible: view.selectedId.length > 0
                    enabledByParent: view.planLoaded
                    onClicked: modPicker.openFor(view.selectedId)
                }
                AppButton {
                    text: qsTr("去出餐")
                    iconName: "serve"
                    enabledByParent: CookSession.hasPlan
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

    Connections {
        target: CookSession
        function onChanged() {
            if (!loaderVersionField.activeFocus)
                loaderVersionField.text = CookSession.loaderVersion;
        }
    }

    Component.onCompleted: {
        selectInitialVersion();
        if (VersionService.versions.rowCount() === 0)
            VersionService.refresh();
        if (CookSession.hasLoader)
            CookSession.refreshLoaderVersions();
    }
}
