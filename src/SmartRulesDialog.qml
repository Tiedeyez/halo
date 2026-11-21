import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// The rules of a smart playlist: songs from your library that match all
// (or any) of them, sorted and optionally limited. A live count shows what
// the rules catch before saving.
Popup {
    id: root
    modal: true
    focus: true
    padding: 24
    width: Math.min(parent ? parent.width - 48 : 820, 820)
    height: Math.min(parent ? parent.height - 48 : 720, 720)
    anchors.centerIn: Overlay.overlay
    Material.roundedScale: Style.radius

    property string playlistId: ""   // empty: a new smart playlist
    signal saved(string id)

    readonly property var fields: [
        { key: "artist", label: qsTr("Artist"), kind: "text" },
        { key: "title", label: qsTr("Title"), kind: "text" },
        { key: "album", label: qsTr("Album"), kind: "text" },
        { key: "genre", label: qsTr("Genre"), kind: "text" },
        { key: "folder", label: qsTr("Folder"), kind: "text" },
        { key: "year", label: qsTr("Year"), kind: "number" },
        { key: "length", label: qsTr("Length (minutes)"), kind: "number" },
        { key: "plays", label: qsTr("Plays"), kind: "number" },
        { key: "bpm", label: qsTr("Tempo (BPM)"), kind: "number" },
        { key: "loudness", label: qsTr("Loudness (LUFS)"), kind: "number" },
        { key: "added", label: qsTr("Added"), kind: "date" },
        { key: "played", label: qsTr("Last played"), kind: "date" }
    ]
    readonly property var ops: ({
        "text": [{ key: "contains", label: qsTr("contains") }, { key: "notContains", label: qsTr("doesn't contain") },
                 { key: "is", label: qsTr("is") }, { key: "isNot", label: qsTr("is not") },
                 { key: "startsWith", label: qsTr("starts with") }],
        "number": [{ key: "greater", label: qsTr("is more than") }, { key: "less", label: qsTr("is less than") },
                   { key: "is", label: qsTr("is") }, { key: "isNot", label: qsTr("is not") }],
        "date": [{ key: "inLast", label: qsTr("in the last") }, { key: "notInLast", label: qsTr("not in the last") }],
        "played": [{ key: "inLast", label: qsTr("in the last") }, { key: "notInLast", label: qsTr("not in the last") },
                   { key: "never", label: qsTr("never") }]
    })
    readonly property var sorts: [
        { key: "added", label: qsTr("Date added") }, { key: "plays", label: qsTr("Plays") },
        { key: "played", label: qsTr("Last played") }, { key: "artist", label: qsTr("Artist") },
        { key: "album", label: qsTr("Album") }, { key: "title", label: qsTr("Title") },
        { key: "year", label: qsTr("Year") }, { key: "bpm", label: qsTr("Tempo") },
        { key: "loudness", label: qsTr("Loudness") }, { key: "random", label: qsTr("Random, new each day") }
    ]
    // Starting points, like a smart playlist template.
    readonly property var presets: [
        { name: qsTr("Heavy rotation"), rules: [{ field: "plays", op: "greater", value: "4" }, { field: "played", op: "inLast", value: "30" }], sort: "plays", limit: 50 },
        { name: qsTr("Recently added"), rules: [{ field: "added", op: "inLast", value: "30" }], sort: "added", limit: 0 },
        { name: qsTr("Never played"), rules: [{ field: "played", op: "never", value: "" }], sort: "random", limit: 100 },
        { name: qsTr("Up-tempo"), rules: [{ field: "bpm", op: "greater", value: "118" }], sort: "bpm", limit: 0 },
        { name: qsTr("Slow and quiet"), rules: [{ field: "bpm", op: "less", value: "95" }, { field: "loudness", op: "less", value: "-12" }], sort: "random", limit: 0 }
    ]

    function fieldIndex(key) { return Math.max(0, fields.findIndex(f => f.key === key)) }
    function opsFor(field) { return field === "played" ? ops.played : ops[fields[fieldIndex(field)].kind] }
    function opIndex(field, op) { return Math.max(0, opsFor(field).findIndex(o => o.key === op)) }

    function spec() {
        const rules = []
        for (let i = 0; i < rulesModel.count; ++i) {
            const r = rulesModel.get(i)
            rules.push({ field: r.field, op: r.op, value: r.value })
        }
        return { name: nameField.text, matchAll: matchBox.currentIndex === 0, rules: rules,
                 sort: sorts[sortBox.currentIndex].key, descending: !ascending.checked,
                 limit: limitOn.checked ? limitBox.value : 0 }
    }

    function load(s) {
        nameField.text = s.name || ""
        matchBox.currentIndex = s.matchAll === false ? 1 : 0
        rulesModel.clear()
        for (const r of (s.rules || []))
            rulesModel.append({ field: r.field, op: r.op, value: String(r.value || "") })
        if (rulesModel.count === 0)
            rulesModel.append({ field: "artist", op: "contains", value: "" })
        sortBox.currentIndex = Math.max(0, sorts.findIndex(x => x.key === (s.sort || "added")))
        ascending.checked = s.descending === false
        limitOn.checked = (s.limit || 0) > 0
        limitBox.value = (s.limit || 0) > 0 ? s.limit : 50
        recount.restart()
    }

    function openFor(id) {
        playlistId = id || ""
        load(playlistId.length ? playlists.spec(playlistId) : { rules: [{ field: "artist", op: "contains", value: "" }] })
        open()
        nameField.forceActiveFocus()
    }

    function save() {
        const s = spec()
        if (!s.name.trim().length) {
            nameField.forceActiveFocus()
            return
        }
        let id = playlistId
        if (id.length)
            playlists.setSpec(id, s)
        else
            id = playlists.createSmart(s.name, s)
        if (id.length) {
            close()
            saved(id)
        }
    }

    property int matching: 0
    Timer { id: recount; interval: 250; onTriggered: root.matching = playlists.previewCount(root.spec()) }

    background: Rectangle {
        color: Style.raised
        radius: Math.min(10, Style.radius)
        border.color: Style.line
        PixelBrackets { shown: true; arm: 16; thickness: 3 }
    }

    ListModel { id: rulesModel }

    contentItem: ColumnLayout {
        spacing: 14
        Keys.onEscapePressed: root.close()

        RowLayout {
            Layout.fillWidth: true
            Glyph { name: "smart"; color: Style.accentText; width: 24; height: 24 }
            ScrambleText {
                target: root.playlistId.length ? qsTr("Edit smart playlist") : qsTr("New smart playlist")
                color: Style.text
                font.pixelSize: Style.fontTitle - 2
                font.weight: Font.Bold
            }
            Item { Layout.fillWidth: true }
            IconButton { glyph: "close"; tip: qsTr("Close (Esc)"); onClicked: root.close() }
        }

        TextField {
            // Selected words in the accent, in the text colour that reads on it.
            selectionColor: Style.accentFill
            selectedTextColor: Style.accentInk
            id: nameField
            Layout.fillWidth: true
            placeholderText: qsTr("Name")
            maximumLength: 120
            Material.accent: Style.accent
            Accessible.name: qsTr("Playlist name")
            Keys.onReturnPressed: root.save()
        }

        Flow {
            Layout.fillWidth: true
            visible: root.playlistId.length === 0
            spacing: 8
            Text { text: qsTr("Start from:"); color: Style.faint; font.pixelSize: Style.fontSmall; height: 32; verticalAlignment: Text.AlignVCenter }
            Repeater {
                model: root.presets
                delegate: Button {
                    required property var modelData
                    text: modelData.name
                    flat: true
                    focusPolicy: Qt.NoFocus
                    implicitHeight: 36
                    Material.roundedScale: Style.radius
                    onClicked: root.load({ name: modelData.name, rules: modelData.rules, sort: modelData.sort, limit: modelData.limit, matchAll: true })
                }
            }
        }

        RowLayout {
            spacing: 8
            Text { text: qsTr("Songs that match"); color: Style.muted; font.pixelSize: Style.fontBody }
            ComboBox {
                id: matchBox
                model: [qsTr("all"), qsTr("any")]
                implicitWidth: 100
                onActivated: recount.restart()
            }
            Text { text: qsTr("of these rules"); color: Style.muted; font.pixelSize: Style.fontBody }
        }

        ListView {
            id: ruleList
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: 120
            clip: true
            spacing: 6
            model: rulesModel
            ScrollBar.vertical: PixelScrollBar {}
            delegate: RowLayout {
                id: ruleRow
                required property int index
                required property string field
                required property string op
                required property string value
                width: ruleList.width - 12
                spacing: 8
                ComboBox {
                    Layout.preferredWidth: 200
                    Accessible.name: qsTr("What the rule looks at")
                    model: root.fields.map(f => f.label)
                    currentIndex: root.fieldIndex(ruleRow.field)
                    onActivated: i => {
                        const key = root.fields[i].key
                        const first = root.opsFor(key)[0].key
                        rulesModel.set(ruleRow.index, { field: key, op: first, value: "" })
                        recount.restart()
                    }
                }
                ComboBox {
                    Layout.preferredWidth: 190
                    Accessible.name: qsTr("How it compares")
                    model: root.opsFor(ruleRow.field).map(o => o.label)
                    currentIndex: root.opIndex(ruleRow.field, ruleRow.op)
                    onActivated: i => { rulesModel.setProperty(ruleRow.index, "op", root.opsFor(ruleRow.field)[i].key); recount.restart() }
                }
                TextField {
                    // Selected words in the accent, in the text colour that reads on it.
                    selectionColor: Style.accentFill
                    selectedTextColor: Style.accentInk
                    Layout.fillWidth: true
                    visible: ruleRow.op !== "never"
                    text: ruleRow.value
                    maximumLength: 200
                    inputMethodHints: root.fields[root.fieldIndex(ruleRow.field)].kind === "text" ? Qt.ImhNone : Qt.ImhFormattedNumbersOnly
                    placeholderText: root.fields[root.fieldIndex(ruleRow.field)].kind === "text" ? qsTr("text") : qsTr("number")
                    Material.accent: Style.accent
                    onTextEdited: { rulesModel.setProperty(ruleRow.index, "value", text); recount.restart() }
                }
                Item { Layout.fillWidth: true; visible: ruleRow.op === "never" }
                Text {
                    visible: root.fields[root.fieldIndex(ruleRow.field)].kind === "date" && ruleRow.op !== "never"
                    text: qsTr("days")
                    color: Style.muted
                    font.pixelSize: Style.fontBody
                }
                IconButton {
                    glyph: "delete"
                    tip: qsTr("Remove this rule")
                    enabled: rulesModel.count > 1
                    onClicked: { rulesModel.remove(ruleRow.index); recount.restart() }
                }
            }
            footer: Button {
                text: qsTr("Add a rule")
                flat: true
                enabled: rulesModel.count < 50
                focusPolicy: Qt.NoFocus
                Material.foreground: Style.accentText
                onClicked: { rulesModel.append({ field: "artist", op: "contains", value: "" }); recount.restart() }
            }
        }

        RowLayout {
            spacing: 8
            Text { text: qsTr("Sort by"); color: Style.muted; font.pixelSize: Style.fontBody }
            ComboBox {
                id: sortBox
                Layout.preferredWidth: 220
                model: root.sorts.map(s => s.label)
                onActivated: recount.restart()
            }
            CheckBox {
                id: ascending
                text: qsTr("Oldest, lowest or A first")
                visible: root.sorts[sortBox.currentIndex].key !== "random"
                Material.accent: Style.accent
            }
        }
        RowLayout {
            spacing: 8
            CheckBox {
                id: limitOn
                text: qsTr("Limit to")
                Material.accent: Style.accent
                onToggled: recount.restart()
            }
            SpinBox {
                id: limitBox
                enabled: limitOn.checked
                from: 1
                to: 10000
                editable: true
                onValueModified: recount.restart()
            }
            Text { text: qsTr("songs"); color: Style.muted; font.pixelSize: Style.fontBody }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                text: qsTr("%n songs match right now", "", root.matching)
                color: root.matching > 0 ? Style.accentText : Style.faint
                font.pixelSize: Style.fontBody
                font.weight: Font.DemiBold
            }
            Button { text: qsTr("Cancel"); flat: true; onClicked: root.close() }
            AccentButton {
                text: qsTr("Save")
                enabled: nameField.text.trim().length > 0
                onClicked: root.save()
            }
        }
    }
}
