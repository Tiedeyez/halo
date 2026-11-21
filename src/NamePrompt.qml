import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// Asks for a name: a new playlist, or a new name for one.
Popup {
    id: root
    modal: true
    focus: true
    padding: 24
    width: Math.min(parent ? parent.width - 48 : 480, 480)
    anchors.centerIn: Overlay.overlay
    Material.roundedScale: Style.radius

    property string heading
    property string action: qsTr("Create")
    property var accepted: null   // function(name)

    function ask(heading, action, initial, done) {
        root.heading = heading
        root.action = action
        root.accepted = done
        field.text = initial || ""
        open()
        field.forceActiveFocus()
        field.selectAll()
    }
    function finish() {
        const name = field.text.trim()
        if (!name.length)
            return
        close()
        if (root.accepted)
            root.accepted(name)
    }

    background: Rectangle {
        color: Style.raised
        radius: Math.min(10, Style.radius)
        border.color: Style.line
        PixelBrackets { shown: true; arm: 16; thickness: 3 }
    }
    contentItem: ColumnLayout {
        spacing: 16
        ScrambleText {
            target: root.heading
            color: Style.text
            font.pixelSize: Style.fontTitle - 2
            font.weight: Font.Bold
        }
        TextField {
            // Selected words in the accent, in the text colour that reads on it.
            selectionColor: Style.accentFill
            selectedTextColor: Style.accentInk
            id: field
            Layout.fillWidth: true
            placeholderText: qsTr("Name")
            maximumLength: 120
            Material.accent: Style.accent
            Accessible.name: root.heading
            Keys.onReturnPressed: root.finish()
            Keys.onEnterPressed: root.finish()
            Keys.onEscapePressed: root.close()
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            Button { text: qsTr("Cancel"); flat: true; onClicked: root.close() }
            AccentButton {
                text: root.action
                enabled: field.text.trim().length > 0
                onClicked: root.finish()
            }
        }
    }
}
