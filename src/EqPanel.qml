import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Halo

// Ten bands, a preamp, presets, and the loudness switch.
Popup {
    id: root
    modal: true
    focus: true
    padding: 24
    width: Math.min(parent ? parent.width - 48 : 760, 760)
    anchors.centerIn: Overlay.overlay
    Material.roundedScale: Style.radius

    readonly property var labels: ["31", "62", "125", "250", "500", "1k", "2k", "4k", "8k", "16k"]

    background: Rectangle {
        color: Style.raised
        radius: Math.min(10, Style.radius)
        border.color: Style.line
        PixelBrackets { shown: true; arm: 16; thickness: 3 }
    }

    contentItem: ColumnLayout {
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            ScrambleText {
                target: qsTr("Equalizer")
                color: Style.text
                font.pixelSize: Style.fontTitle - 2
                font.weight: Font.Bold
            }
            Item { Layout.fillWidth: true }
            Switch {
                text: player.eqEnabled ? qsTr("On") : qsTr("Off")
                checked: player.eqEnabled
                onToggled: player.eqEnabled = checked
                Material.accent: Style.accent
                focusPolicy: Qt.NoFocus
            }
            IconButton { glyph: "close"; tip: qsTr("Close (Esc)"); onClicked: root.close() }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: player.eqPresets
                AccentButton {
                    required property string modelData
                    text: qsTr(modelData)
                    checkable: true
                    checked: player.eqPreset === modelData && player.eqEnabled
                    filled: checked
                    leftPadding: 14
                    rightPadding: 14
                    focusPolicy: Qt.NoFocus
                    onClicked: player.applyEqPreset(modelData)
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            spacing: 4

            ColumnLayout {
                Layout.fillHeight: true
                Slider {
                    Layout.fillHeight: true
                    Layout.alignment: Qt.AlignHCenter
                    orientation: Qt.Vertical
                    from: -12; to: 12; stepSize: 0.5
                    value: player.eqPreamp
                    onMoved: player.eqPreamp = value
                    Material.accent: Style.accent
                    Accessible.name: qsTr("Preamp")
                }
                Text { Layout.alignment: Qt.AlignHCenter; text: qsTr("Pre"); color: Style.muted; font.pixelSize: Style.fontSmall }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: (player.eqPreamp > 0 ? "+" : "") + player.eqPreamp.toLocaleString(Qt.locale(translator.language), 'f', 1)
                    color: Style.faint
                    font.pixelSize: Style.fontSmall
                }
            }
            Rectangle { Layout.fillHeight: true; Layout.preferredWidth: 1; Layout.margins: 8; color: Style.line }

            Repeater {
                model: 10
                ColumnLayout {
                    id: band
                    required property int index
                    Layout.fillHeight: true
                    Layout.fillWidth: true
                    Slider {
                        Layout.fillHeight: true
                        Layout.alignment: Qt.AlignHCenter
                        orientation: Qt.Vertical
                        from: -12; to: 12; stepSize: 0.5
                        value: player.eqGains[band.index]
                        onMoved: player.setEqGain(band.index, value)
                        Material.accent: Style.accent
                        Accessible.name: qsTr("%1 Hz").arg(root.labels[band.index])
                    }
                    Text { Layout.alignment: Qt.AlignHCenter; text: root.labels[band.index]; color: Style.muted; font.pixelSize: Style.fontSmall }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: (player.eqGains[band.index] > 0 ? "+" : "") + Number(player.eqGains[band.index]).toLocaleString(Qt.locale(translator.language), 'f', 1)
                        color: Style.faint
                        font.pixelSize: Style.fontSmall
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Style.line }

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Even loudness"); color: Style.text; font.pixelSize: Style.fontBody }
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Brings every song to the same loudness (-14 LUFS), measured by Halo, so a quiet album and a loud mixtape sit together.")
                    color: Style.faint
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }
            Switch {
                checked: player.normalize
                onToggled: player.normalize = checked
                Material.accent: Style.accent
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Even loudness")
            }
        }

        // Headphone correction: found by the output's name, on by itself.
        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Headphone correction"); color: Style.text; font.pixelSize: Style.fontBody }
                Text {
                    Layout.fillWidth: true
                    textFormat: Text.PlainText
                    text: headphones.matched
                          ? qsTr("%1, measured by %2. Makes them neutral; the equalizer above is then your taste on top.")
                                .arg(headphones.model).arg(headphones.source)
                          : qsTr("Nothing to correct on %1. Halo knows %2 headphones by name; Bluetooth and USB ones name themselves, ones in a headphone jack can't be told apart.")
                                .arg(headphones.device.length > 0 ? headphones.device : qsTr("this output")).arg(headphones.known)
                    color: Style.faint
                    font.pixelSize: Style.fontSmall
                    wrapMode: Text.Wrap
                }
            }
            Switch {
                visible: headphones.matched
                checked: headphones.active
                onToggled: headphones.setActive(checked)
                Material.accent: Style.accent
                focusPolicy: Qt.NoFocus
                Accessible.name: qsTr("Headphone correction")
            }
        }
    }
}
