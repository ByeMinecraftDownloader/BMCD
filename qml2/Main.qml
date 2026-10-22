import QtQuick
import BMCD
import BMCD.UI

Window {
    id: root

    width: initialWidth
    height: initialHeight
    minimumWidth: 880
    minimumHeight: 600
    visible: true
    color: Tokens.background
    title: "BMCD"
    flags: Qt.Window | Qt.FramelessWindowHint

    property int section: initialSection
    property bool downloadsRunning: VersionDetail.downloadState === "running"
                                         || VersionDetail.downloadState === "paused"

    // MC_AUTOSTART is the screenshot/deep-link helper: load the plan for the
    // preselected version even when the window opens on cook/serve directly.
    Component.onCompleted: {
        if (typeof autoStartDownload !== "undefined" && autoStartDownload
                && typeof initialVersion !== "undefined" && initialVersion.length > 0
                && VersionDetail.versionId !== initialVersion)
            VersionDetail.select(initialVersion, VersionService.urlFor(initialVersion));
    }

    // Plain versions are cooked by VersionDetail into downloads/versions/<id>.
    // When we enter 出餐 after that download, rebuild the plan against that
    // exact tree instead of PackController's private cooking directory.
    function ensureDownloadedPlan() {
        if (VersionDetail.state !== "ready" || VersionDetail.downloadState !== "done")
            return false;
        // A mods-only pack has no vanilla files, so it must not be replaced by
        // the version download that cooked alongside it.
        if (PackController.fileCount > 0 && !PackController.hasGameFiles)
            return true;
        if (PackController.fileCount > 0
                && PackController.versionId === VersionDetail.versionId
                && PackController.adopted
                && PackController.sourceRoot === VersionDetail.targetDir)
            return true;
        PackController.prepareDownloaded(VersionDetail.versionId,
                                         VersionService.urlFor(VersionDetail.versionId),
                                         VersionDetail.targetDir);
        return true;
    }

    Column {
        anchors.fill: parent

        Item {
            width: parent.width
            height: 40

            WindowChrome {
                anchors.fill: parent
                title: "BMCD"
                subtitle: "Bye!MinecraftDownloader"
            }
        }

        Row {
            width: parent.width
            height: parent.height - 40

            NavRail {
                id: rail
                current: root.section
                onSelect: function(index) { root.section = index }
            }

            Item {
                id: contentHost
                width: parent.width - rail.width
                height: parent.height
                readonly property int gutter: width < 1000 ? 12 : 16

                Item {
                    id: page
                    anchors.centerIn: parent
                    width: Math.min(parent.width - contentHost.gutter * 2, 1280)
                    height: parent.height - contentHost.gutter * 2

                Loader {
                    anchors.fill: parent
                    active: root.section === 0
                    source: "views/OrderView.qml"
                    onLoaded: {
                        item.openCook.connect(function() { root.section = 1; })
                        item.openSettings.connect(function() { root.section = 3; })
                        item.openServe.connect(function() {
                            root.section = 2;
                            Qt.callLater(function() {
                                if (root.ensureDownloadedPlan())
                                    return;
                                if (PackController.fileCount === 0 && VersionDetail.state === "ready")
                                    PackController.prepare(VersionDetail.versionId,
                                                           VersionService.urlFor(VersionDetail.versionId));
                            });
                        })
                    }
                }
                Loader {
                    anchors.fill: parent
                    active: root.section === 1
                    source: "views/CookView.qml"
                    onLoaded: {
                        item.openOrder.connect(function() { root.section = 0; })
                        item.openServe.connect(function() {
                            root.section = 2;
                            Qt.callLater(function() { root.ensureDownloadedPlan(); });
                        })
                    }
                }
                Loader {
                    anchors.fill: parent
                    active: root.section === 2
                    source: "views/ServeView.qml"
                    onLoaded: item.openOrder.connect(function() { root.section = 0; })
                }
                Loader {
                    anchors.fill: parent
                    active: root.section === 3
                    source: "views/SettingsView.qml"
                }
                }
            }
        }
    }

    // Cook is global: a 2 px line under the chrome while downloads run.
    Rectangle {
        anchors.top: parent.top
        anchors.topMargin: 40
        anchors.left: parent.left
        anchors.right: parent.right
        height: 2
        visible: root.downloadsRunning
        color: Tokens.surfaceHigh

        Rectangle {
            height: parent.height
            width: parent.width * (VersionDetail.downloadTotalBytes > 0
                                   ? VersionDetail.doneBytes / VersionDetail.downloadTotalBytes : 0)
            color: Tokens.primary
            Behavior on width { NumberAnimation { duration: Motion.standard } }
        }
    }

    WindowResizeHandles { }

    ToastBar {
        id: toast
        anchors.horizontalCenter: parent.horizontalCenter
    }

    Connections {
        target: VersionDetail
        function onMessage(text) { toast.show(text); }
    }
    Connections {
        target: PackController
        function onMessage(text) { toast.show(text); }
    }

    Shortcut {
        sequence: "/"
        onActivated: {
            root.section = 0;
            const order = root.contentChildren;
        }
    }
    Shortcut {
        sequence: "Ctrl+1"
        onActivated: root.section = 0
    }
    Shortcut {
        sequence: "Ctrl+2"
        onActivated: root.section = 1
    }
    Shortcut {
        sequence: "Ctrl+3"
        onActivated: root.section = 2
    }
    Shortcut {
        sequence: "Ctrl+4"
        onActivated: root.section = 3
    }
    Shortcut {
        sequence: "Escape"
        onActivated: VersionDetail.clear()
    }
}
