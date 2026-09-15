import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Workbench

// 素材库主界面（对应旧版 index.html 的 .app 主体）：
//   顶部工具条（搜索 / 计数 / 重新扫描 / 切换账号）
//   左：筛选栏（视图筛选 + 标签筛选）  中：素材网格  右：详情 + 文件夹树
//   底：操作栏（调用次数 / 打标 / 删除 / 新建分类文件 / 打开文件夹）
Item {
    id: view

    // ---------------------------------------------------------------- 状态
    property var categories: ({ "shot": [], "product": [], "availability": [] })
    property var selShot: []
    property var selProduct: []
    property var selAvail: []
    property string andOr: "and"

    property string searchText: ""
    property string sortBy: ""
    property string sortDir: "desc"
    property string activeFolder: ""

    // 视图筛选（新增）：调用次数下限 + 只看打过标签的
    property int minUsage: 0
    property bool onlyTagged: false

    property var videos: []
    property int total: 0
    property bool loading: false

    property var selectedIds: []
    property int detailId: 0
    property var detail: ({})

    property var folders: []
    property var folderSel: []
    property var folderOpen: ({})
    property string lastFolder: ""

    property bool tagDialogShown: false
    property var tagTargetIds: []
    property var commonTags: []

    property int playerId: 0

    property bool confirmShown: false
    property string confirmText: ""
    property bool confirmDanger: false        // 危险操作：确认按钮走红色
    property string confirmOkText: ""         // 确认按钮文案（空 = 「确定」）
    property string confirmFolder: ""
    property var pendingDeleteFolders: []
    property var pendingDeleteVideos: []
    property int pendingRemoveTag: -1

    // 分页：几千条素材一次性构造再塞给 GridView，光数据搬运就够卡了，
    // 所以一页一页取、滚到底再要下一页。
    property int pageSize: 300
    property bool hasMore: false
    property bool loadingMore: false

    // ---------------------------------------------------------------- 数据
    function reloadCategories() {
        view.categories = Library.categories()
    }

    function filterMap(offset) {
        return {
            "shot": view.selShot,
            "product": view.selProduct,
            "avail": view.selAvail,
            "andOr": view.andOr,
            "search": view.searchText,
            "folder": view.activeFolder,
            "sortBy": view.sortBy,
            "sortDir": view.sortDir,
            "minUsage": view.minUsage,
            "onlyTagged": view.onlyTagged,
            "offset": offset,
            "limit": view.pageSize
        }
    }

    function applyFilter() {
        view.loading = true
        const r = Library.query(view.filterMap(0))

        // 共享库偶发读失败（网络盘上的 SQLite 会读到别台机器正在写的页）：
        // 隔一小会儿整页重试一次，不然界面会毫无理由地白成「0 个视频」、
        // 左侧分类也一起空掉。
        if (r.readError) {
            view.loading = false
            if (retryTimer.retries < 5) {
                retryTimer.restart()
            } else {
                retryTimer.retries = 0
                toast.show(qsTr("共享库暂时读不到数据（网络不稳），请稍后重试"))
            }
            return
        }

        retryTimer.retries = 0
        view.videos = r.items
        view.total = r.total
        view.hasMore = r.hasMore === true
        view.loading = false
    }

    // 再取一页追加到列表末尾（滚到接近底部时由 GridPane 触发）
    function loadMore() {
        if (!view.hasMore || view.loadingMore || view.loading)
            return
        view.loadingMore = true
        const r = Library.query(view.filterMap(view.videos.length))
        if (!r.readError && r.items && r.items.length > 0) {
            view.videos = view.videos.concat(r.items)
            view.total = r.total
            view.hasMore = r.hasMore === true
        } else {
            view.hasMore = false
        }
        view.loadingMore = false
    }

    // 读库失败后的重试节拍：次数由 retries 计数控制，触发时整页刷新
    // （分类、文件夹树、详情也都可能踩到同一个坏页，一起重来更稳）
    Timer {
        id: retryTimer
        interval: 300
        property int retries: 0
        onTriggered: {
            retries = retries + 1
            view.refreshAll()
        }
    }

    function reloadFolders() {
        view.folders = Library.folders()
    }

    function refreshAll() {
        reloadCategories()
        reloadFolders()
        applyFilter()
        if (view.detailId > 0)
            view.detail = Library.videoDetail(view.detailId)
    }

    function selectVideo(id) {
        view.detailId = id
        view.detail = Library.videoDetail(id)
    }

    // 点播放键 / 双击卡片 → 打开播放器浮层（与旧版一致）
    function openVideo(item) {
        if (!item)
            return
        view.selectVideo(item.id)
        player.open(item)
    }

    function toggleTag(cat, tagId) {
        const key = cat === "shot" ? "selShot" : (cat === "product" ? "selProduct" : "selAvail")
        let arr = view[key].slice()
        const i = arr.indexOf(tagId)
        if (i >= 0)
            arr.splice(i, 1)
        else
            arr.push(tagId)
        view[key] = arr
        applyFilter()
    }

    function createTag(cat) {
        tagInput.cat = cat
        tagInput.shown = true
        tagInput.text = ""
    }

    // 统一的确认框入口：先清掉上一次的待办，避免残留状态串台
    function askConfirm(text, opts) {
        const o = opts || {}
        view.confirmText = text
        view.confirmFolder = o.folder || ""
        view.confirmDanger = o.danger === true
        view.confirmOkText = o.okText || ""
        view.pendingDeleteFolders = o.folders || []
        view.pendingDeleteVideos = o.videos || []
        view.pendingRemoveTag = o.tagId || -1
        view.confirmShown = true
    }

    function closeConfirm() {
        view.confirmShown = false
        view.confirmFolder = ""
        view.confirmDanger = false
        view.confirmOkText = ""
        view.pendingDeleteFolders = []
        view.pendingDeleteVideos = []
        view.pendingRemoveTag = -1
    }

    function removeTag(cat, tagId, name) {
        view.askConfirm(qsTr("确定删除标签「") + name
                        + qsTr("」？\n\n仅从数据库移除该标签，不会删除或改动任何视频文件。"),
                        { tagId: tagId })
    }

    function adjustUsage(delta) {
        if (view.selectedIds.length === 0)
            return
        Library.adjustUsage(view.selectedIds, delta)
        view.refreshAll()
    }

    function doCopy() {
        if (view.selectedIds.length === 0)
            return
        const r = Library.createCategoryFolder(view.selectedIds, App.account)
        if (r.ok) {
            view.lastFolder = r.folder
            // 新文件夹要立刻出现在右侧文件夹树里：以前这里没刷新，
            // 建完看着像「没成功」，非得重开程序才看得到。
            view.reloadFolders()
            view.askConfirm(qsTr("已复制 ") + r.count + qsTr(" 个视频到分类文件夹（原视频未动）：\n\n")
                            + r.folder + qsTr("\n\n是否打开该文件夹？"),
                            { folder: r.folder, okText: qsTr("打开文件夹") })
        } else {
            toast.show(r.message || qsTr("创建失败"))
        }
    }

    // ---- 删除 ----
    // 能进回收站就进回收站；共享盘（\\server\share\…）在 Windows 上没有回收站，
    // 只能直接删除，所以确认框里要提前讲清楚「删了找不回来」。
    function requestDelete(ids) {
        if (!ids || ids.length === 0)
            return
        const names = []
        for (let i = 0; i < ids.length && i < 3; ++i) {
            const d = Library.videoDetail(ids[i])
            names.push(d.video ? d.video.name : ("#" + ids[i]))
        }

        const paths = Library.pathsOfVideos(ids)
        let networked = false
        for (let i = 0; i < paths.length; ++i) {
            if (paths[i].indexOf("\\\\") === 0 || paths[i].indexOf("//") === 0)
                networked = true
        }

        let text = qsTr("确定删除选中的 ") + ids.length + qsTr(" 个视频？")
        if (names.length > 0)
            text += "\n\n" + names.join("\n") + (ids.length > names.length ? "\n…" : "")
        text += networked
                ? qsTr("\n\n这些素材在共享盘上，Windows 回收站不支持网络位置 —— 删除后无法找回，请确认。")
                : qsTr("\n\n文件会进入 Windows 回收站（可以从回收站还原），数据库记录同时移除。")
        view.askConfirm(text, { videos: ids, danger: true, okText: qsTr("删除") })
    }

    function doDeleteVideos() {
        const ids = view.pendingDeleteVideos.slice()
        view.pendingDeleteVideos = []
        if (ids.length === 0)
            return

        const r = Library.deleteVideos(ids)
        const removed = r.removed ? r.removed.length : 0

        if (removed > 0) {
            view.selectedIds = view.selectedIds.filter(x => ids.indexOf(x) < 0)
            if (ids.indexOf(view.detailId) >= 0) {
                view.detailId = 0
                view.detail = ({})
            }
            // 正在播的就是被删掉的 → 直接把播放器关掉，免得继续播一个已经不存在的文件
            if (ids.indexOf(player.currentId) >= 0)
                player.closed()
            const perm = r.permanent || 0
            toast.show(perm > 0
                       ? (qsTr("已删除 ") + removed + qsTr(" 个视频（其中 ") + perm
                          + qsTr(" 个在共享盘上，回收站里找不回来）"))
                       : (qsTr("已把 ") + removed + qsTr(" 个视频移到回收站")))
        }
        if (r.errors && r.errors.length > 0)
            toast.show(r.errors[0].error || qsTr("删除失败"))

        view.refreshAll()
    }

    function openTagDialogFor(ids) {
        if (!ids || ids.length === 0)
            return
        // 先关再开：弹层可见性完全由 tagDialogShown 驱动，这里保证每次打开都是一次干净的
        // false → true（此前「ESC 关掉之后再点按钮弹不出来」就是状态没被重置导致的）。
        view.tagDialogShown = false
        view.tagTargetIds = ids.slice()
        // 多个视频时取标签交集（与旧版一致）
        let common = null
        for (let i = 0; i < ids.length && i < 60; ++i) {
            const d = Library.videoDetail(ids[i])
            const set = (d.tags || []).map(t => t.id)
            common = (common === null) ? set : common.filter(x => set.indexOf(x) >= 0)
        }
        view.commonTags = common || []
        Qt.callLater(function () { view.tagDialogShown = true })
    }

    Component.onCompleted: {
        reloadCategories()
        reloadFolders()
        applyFilter()
    }

    // 扫描跑完自动刷新列表与文件夹树。
    // 以前扫完不做任何刷新，界面上还是旧数据，看起来像「重新扫描没生效」。
    Connections {
        target: Scanner
        function onFinished(fileCount) {
            view.refreshAll()
        }
    }

    // 共享库是多人同时在用的：定时探一次库版本号，别人在别的电脑上打了标签 / 加了新素材，
    // 这里会自动跟着刷新（以前只有手动「重新扫描」，而且扫完还不刷新界面）。
    property int seenDbVersion: -1
    Timer {
        interval: 15000
        running: true
        repeat: true
        onTriggered: {
            App.refreshDbVersion()
            // 正在扫描时不要刷：库里每批都在变，刷了也白刷，反而和扫描抢 I/O 把界面拖卡
            if (Scanner.scanning)
                return
            if (App.dbVersion !== view.seenDbVersion) {
                view.seenDbVersion = App.dbVersion
                view.refreshAll()
            }
        }
    }

    // ---------------------------------------------------------------- 界面
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        // ============================================================ 顶部工具条
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 48
            radius: Theme.radiusCard
            color: Theme.panel
            border.width: 1
            border.color: Theme.line

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 10
                spacing: 10

                Rectangle {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 520
                    implicitHeight: 34
                    radius: 11
                    color: Theme.panel2
                    border.width: 1.5
                    border.color: searchField.activeFocus ? Theme.brand : Theme.line

                    Behavior on border.color { ColorAnimation { duration: Theme.durBase } }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 11
                        anchors.rightMargin: 10
                        spacing: 7
                        Text {
                            text: "🔍"
                            color: Theme.soft
                            font.pixelSize: 12
                        }
                        TextField {
                            id: searchField
                            Layout.fillWidth: true
                            placeholderText: qsTr("按标签搜索（模糊匹配）")
                            color: Theme.txt
                            font.pixelSize: 12
                            background: Item {}
                            onTextChanged: searchDebounce.restart()
                        }
                        Text {
                            visible: searchField.text !== ""
                            text: "✕"
                            color: Theme.soft
                            font.pixelSize: 12
                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -4
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    searchField.text = ""
                                    view.searchText = ""
                                    view.applyFilter()
                                }
                            }
                        }
                    }
                }

                Timer {
                    id: searchDebounce
                    interval: 260
                    onTriggered: {
                        view.searchText = searchField.text.trim()
                        view.applyFilter()
                    }
                }

                // 计数做成胶囊，和卡片里的「调用 N 次」同一套语言
                Rectangle {
                    implicitWidth: countText.implicitWidth + 22
                    implicitHeight: 26
                    radius: 13
                    color: Theme.panel2
                    border.width: 1
                    border.color: Theme.line

                    Text {
                        id: countText
                        anchors.centerIn: parent
                        text: qsTr("共 ") + view.total
                        color: Theme.sub
                        font.pixelSize: 12
                    }
                }

                Item { Layout.fillWidth: true }

                IconBtn {
                    glyph: "⟳"
                    tip: qsTr("重新扫描素材")
                    active: Scanner.scanning
                    onClicked: Scanner.start()
                }

                IconBtn {
                    glyph: "👤"
                    tip: qsTr("切换账号")
                    onClicked: App.logout()
                }
            }
        }

        // ============================================================ 三栏主体
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Rectangle {
                Layout.preferredWidth: 232
                Layout.fillHeight: true
                radius: Theme.radiusCard
                color: Theme.panel
                border.width: 1
                border.color: Theme.line

                FilterPane {
                    anchors.fill: parent
                    categories: view.categories
                    selectedShot: view.selShot
                    selectedProduct: view.selProduct
                    selectedAvail: view.selAvail
                    andOr: view.andOr
                    minUsage: view.minUsage
                    onlyTagged: view.onlyTagged
                    onToggleTag: (cat, tagId) => view.toggleTag(cat, tagId)
                    onCreateTag: (cat) => view.createTag(cat)
                    onRemoveTag: (cat, tagId, name) => view.removeTag(cat, tagId, name)
                    onSetAndOr: (mode) => {
                        view.andOr = mode
                        view.applyFilter()
                    }
                    onSetMinUsage: (value) => {
                        view.minUsage = value
                        view.applyFilter()
                    }
                    onSetOnlyTagged: (on) => {
                        view.onlyTagged = on
                        view.applyFilter()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: Theme.radiusCard
                color: Theme.panel
                border.width: 1
                border.color: Theme.line

                GridPane {
                    anchors.fill: parent
                    anchors.margins: 10
                    videos: view.videos
                    selectedIds: view.selectedIds
                    total: view.total
                    sortBy: view.sortBy
                    sortDir: view.sortDir
                    loading: view.loading
                    hasMore: view.hasMore
                    onLoadMoreRequested: view.loadMore()
                    onToggleSelect: (id) => {
                        let arr = view.selectedIds.slice()
                        const i = arr.indexOf(id)
                        if (i >= 0)
                            arr.splice(i, 1)
                        else
                            arr.push(id)
                        view.selectedIds = arr
                    }
                    onSetSelectAll: (on) => {
                        view.selectedIds = on ? view.videos.map(v => v.id) : []
                    }
                    onSelectDetail: (id) => view.selectVideo(id)
                    onOpenVideo: (item) => view.openVideo(item)
                    onSortChanged: (by) => {
                        view.sortBy = by
                        view.applyFilter()
                    }
                    onSortDirToggled: {
                        view.sortDir = view.sortDir === "asc" ? "desc" : "asc"
                        view.applyFilter()
                    }
                    onRequestDrag: (ids) => Library.startFileDrag(Library.pathsOfVideos(ids))
                }
            }

            Rectangle {
                Layout.preferredWidth: 300
                Layout.fillHeight: true
                radius: Theme.radiusCard
                color: Theme.panel
                border.width: 1
                border.color: Theme.line

                DetailPane {
                    anchors.fill: parent
                    anchors.margins: 10
                    detail: view.detail
                    folders: view.folders
                    folderSel: view.folderSel
                    folderOpen: view.folderOpen
                    activeFolder: view.activeFolder
                    onRemoveTag: (tagId) => {
                        Library.assignTags([view.detailId], [tagId], "remove")
                        view.refreshAll()
                    }
                    onAddTagRequested: view.openTagDialogFor([view.detailId])
                    onSelectFolder: (path) => {
                        view.activeFolder = (view.activeFolder === path) ? "" : path
                        view.applyFilter()
                    }
                    onToggleFolderOpen: (path) => {
                        let m = {}
                        for (const k in view.folderOpen)
                            m[k] = view.folderOpen[k]
                        m[path] = !(m[path] === true)
                        view.folderOpen = m
                    }
                    onToggleFolderSel: (path) => {
                        let arr = view.folderSel.slice()
                        const i = arr.indexOf(path)
                        if (i >= 0)
                            arr.splice(i, 1)
                        else
                            arr.push(path)
                        view.folderSel = arr
                    }
                    onDragFolderTo: (from, to) => {
                        const r = Library.copyFolders([from], to)
                        toast.show(r.ok ? qsTr("已复制到目标文件夹") : qsTr("复制失败"))
                        view.reloadFolders()
                        view.applyFilter()
                    }
                    onDeleteFolder: (path) => {
                        view.askConfirm(qsTr("确定删除文件夹「") + path
                                        + qsTr("」及其中的全部内容？\n\n此操作不可恢复（受保护目录不会被删除）。"),
                                        { folders: [path], danger: true, okText: qsTr("删除") })
                    }
                    onDeleteSelectedFolders: {
                        if (view.folderSel.length === 0)
                            return
                        view.askConfirm(qsTr("确定删除选中的 ") + view.folderSel.length
                                        + qsTr(" 个文件夹及其全部内容？\n\n此操作不可恢复。"),
                                        { folders: view.folderSel.slice(), danger: true, okText: qsTr("删除") })
                    }
                }
            }
        }

        // ============================================================ 底部操作栏
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 54
            radius: Theme.radiusCard
            color: Theme.panel
            border.width: 1
            border.color: Theme.line

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 12
                spacing: 8

                Text {
                    text: qsTr("已选 ") + view.selectedIds.length
                    color: Theme.txt
                    font.pixelSize: 12
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: view.selectedIds.length
                          ? qsTr("删除会先把文件移进回收站（可还原）；新建分类文件夹只复制，不动原视频")
                          : qsTr("点卡片选中查看详情，点封面 ▶ 播放，勾选可多选")
                    color: Theme.soft
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }

                Repeater {
                    model: [
                        { id: "minus", label: qsTr("－调用") },
                        { id: "plus", label: qsTr("＋调用") },
                        { id: "delete", label: qsTr("删除"), danger: true },
                        { id: "tag", label: qsTr("打标") },
                        { id: "copy", label: qsTr("新建分类文件"), primary: true },
                        { id: "open", label: qsTr("打开文件夹") }
                    ]
                    delegate: Btn {
                        required property var modelData
                        enabled: view.selectedIds.length > 0
                        kind: modelData.primary ? "primary" : (modelData.danger ? "danger" : "soft")
                        text: modelData.label
                        onClicked: {
                            if (view.selectedIds.length === 0)
                                return
                            if (modelData.id === "minus")
                                view.adjustUsage(-1)
                            else if (modelData.id === "plus")
                                view.adjustUsage(1)
                            else if (modelData.id === "delete")
                                view.requestDelete(view.selectedIds)
                            else if (modelData.id === "tag")
                                view.openTagDialogFor(view.selectedIds)
                            else if (modelData.id === "copy")
                                view.doCopy()
                            else
                                Library.openFolder(view.activeFolder || view.lastFolder || App.shareRoot)
                        }
                    }
                }
            }
        }
    }

    // ESC：先退全屏 → 否则关掉最上层的浮层（新建标签输入 / 打标弹层 / 播放器），与旧版行为一致。
    // 用 Shortcut 而不是 Keys，这样不依赖焦点在谁身上。
    Shortcut {
        sequence: "Escape"
        enabled: tagInput.shown || view.tagDialogShown || player.currentId !== 0 || view.confirmShown
        onActivated: {
            if (view.confirmShown)
                view.closeConfirm()
            else if (tagInput.shown)
                tagInput.shown = false
            else if (view.tagDialogShown)
                view.tagDialogShown = false
            else if (player.fullscreen)
                player.exitFullscreen()
            else
                player.closed()
        }
    }

    // ---------------------------------------------------------------- 播放器浮层
    PlayerOverlay {
        id: player
        anchors.fill: parent
        playlist: view.videos
        onClosed: {
            // 关播放器时一定先退全屏：否则窗口还停在全屏，下次打开播放器也直接是全屏
            player.exitFullscreen()
            player.currentId = 0
            player.currentPath = ""
            view.playerId = 0
        }
        onCurrentChanged: (videoId) => {
            view.playerId = videoId
            view.selectVideo(videoId)
        }
        onTagRequested: (videoId, category) => view.openTagDialogFor([videoId])
        onDeleteRequested: (videoId) => view.requestDelete([videoId])
    }

    // ---------------------------------------------------------------- 打标弹层
    TagDialog {
        id: tagDlg
        anchors.fill: parent
        shown: view.tagDialogShown
        categories: view.categories
        videoIds: view.tagTargetIds
        commonTags: view.commonTags
        onToggleTag: (tagId, add) => {
            Library.assignTags(view.tagTargetIds, [tagId], add ? "add" : "remove")
            view.openTagDialogFor(view.tagTargetIds)
            view.refreshAll()
        }
        onCreateTagAndAssign: (category, name) => {
            const r = Library.createTag(category, name)
            if (r.ok) {
                Library.assignTags(view.tagTargetIds, [r.id], "add")
                view.openTagDialogFor(view.tagTargetIds)
                view.refreshAll()
            } else {
                toast.show(r.message || qsTr("创建失败"))
            }
        }
        // 关闭一律由弹层发 closed() 信号驱动。弹层内部绝不能直接写自己的 shown，
        // 否则会打断上面 `shown: view.tagDialogShown` 的绑定 —— 那正是
        // 「第一次能打开、关掉之后点镜头内容/产品/可用性都打不开」的原因。
        onClosed: view.tagDialogShown = false
    }

    // ---------------------------------------------------------------- 新建标签输入
    Item {
        id: tagInput
        property bool shown: false
        property string cat: "shot"
        property alias text: tagInputField.text

        anchors.fill: parent
        visible: shown
        z: 120

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(16 / 255, 22 / 255, 38 / 255, 0.42)
            MouseArea {
                anchors.fill: parent
                onClicked: tagInput.shown = false
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: 348
            height: 148
            radius: Theme.radiusCard
            color: Theme.panel
            border.width: 1
            border.color: Theme.line

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 12

                Text {
                    text: qsTr("新建标签")
                          + (tagInput.cat === "shot" ? qsTr("（镜头内容）")
                             : (tagInput.cat === "product" ? qsTr("（产品）") : qsTr("（可用性）")))
                    color: Theme.txt
                    font.pixelSize: 14
                    font.bold: true
                }
                TextField {
                    id: tagInputField
                    Layout.fillWidth: true
                    placeholderText: qsTr("标签名")
                    font.pixelSize: 13
                    color: Theme.txt
                    background: Rectangle {
                        radius: Theme.radiusSm
                        border.width: 1.5
                        border.color: tagInputField.activeFocus ? Theme.brand : Theme.line
                        color: Theme.panel2
                    }
                    onAccepted: {
                        const r = Library.createTag(tagInput.cat, text.trim())
                        if (r.ok) {
                            tagInput.shown = false
                            view.reloadCategories()
                        } else {
                            toast.show(r.message || qsTr("创建失败"))
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Btn {
                        text: qsTr("取消")
                        onClicked: tagInput.shown = false
                    }
                    Btn {
                        text: qsTr("创建")
                        kind: "primary"
                        // 直接写逻辑而不是调 tagInputField.onAccepted()：
                        // 信号处理器不是普通方法，当函数调用的写法会被 qmllint/qmlsc 判为无效成员。
                        onClicked: {
                            const r = Library.createTag(tagInput.cat, tagInputField.text.trim())
                            if (r.ok) {
                                tagInput.shown = false
                                view.reloadCategories()
                            } else {
                                toast.show(r.message || qsTr("创建失败"))
                            }
                        }
                    }
                }
            }
        }
        onShownChanged: if (shown) tagInputField.forceActiveFocus()
    }

    // ---------------------------------------------------------------- 确认框
    Item {
        anchors.fill: parent
        visible: view.confirmShown
        z: 130

        Rectangle {
            anchors.fill: parent
            color: Qt.rgba(16 / 255, 22 / 255, 38 / 255, 0.42)
            MouseArea {
                anchors.fill: parent
                onClicked: view.closeConfirm()
            }
        }
        Rectangle {
            anchors.centerIn: parent
            width: Math.min(view.width - 80, 560)
            height: confirmBody.implicitHeight + 40
            radius: Theme.radiusCard
            color: Theme.panel
            border.width: 1
            border.color: view.confirmDanger ? Theme.dangerLine : Theme.line

            ColumnLayout {
                id: confirmBody
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14

                Text {
                    Layout.fillWidth: true
                    text: view.confirmText
                    color: Theme.txt
                    font.pixelSize: 13
                    lineHeight: 1.25
                    wrapMode: Text.WrapAnywhere
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    Btn {
                        text: qsTr("取消")
                        onClicked: view.closeConfirm()
                    }
                    Btn {
                        text: view.confirmOkText !== "" ? view.confirmOkText : qsTr("确定")
                        kind: view.confirmDanger ? "danger" : "primary"
                        onClicked: {
                            const folder = view.confirmFolder
                            const folders = view.pendingDeleteFolders.slice()
                            const tagId = view.pendingRemoveTag
                            const hasVideos = view.pendingDeleteVideos.length > 0
                            view.confirmShown = false

                            if (folder !== "") {
                                view.closeConfirm()
                                Library.openFolder(folder)
                            } else if (hasVideos) {
                                view.doDeleteVideos()
                                view.closeConfirm()
                            } else if (folders.length > 0) {
                                view.closeConfirm()
                                Library.deleteFolders(folders)
                                view.folderSel = []
                                view.refreshAll()
                            } else if (tagId > 0) {
                                view.closeConfirm()
                                Library.deleteTag(tagId)
                                view.refreshAll()
                            } else {
                                view.closeConfirm()
                            }
                        }
                    }
                }
            }
        }
    }

    // ---------------------------------------------------------------- 轻提示
    Item {
        id: toast
        property string text: ""
        property bool shown: false

        anchors.fill: parent
        visible: shown
        z: 300

        function show(msg) {
            text = msg
            shown = true
            toastTimer.restart()
        }

        Timer {
            id: toastTimer
            interval: 2400
            onTriggered: toast.shown = false
        }

        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 90
            implicitWidth: toastText.implicitWidth + 34
            implicitHeight: 40
            radius: 12
            color: Qt.rgba(0.09, 0.11, 0.16, 0.94)

            Text {
                id: toastText
                anchors.centerIn: parent
                text: toast.text
                color: "white"
                font.pixelSize: 12
            }
        }
    }
}
