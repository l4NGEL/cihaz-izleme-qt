import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cihaz 1.0
import "Theme.js" as Theme

// Sürücü komutları Modbus holding register'larına yazılır (hedef hız, kontrol sözcüğü)
Card {
    title: "Sürücü kontrolü (Modbus)"
    enabled: App.running && !App.replaying

    RowLayout {
        Layout.fillWidth: true
        Switch {
            id: enable
            text: "Sürücü etkin"
            checked: App.driveEnabled
            onToggled: App.setDriveEnabled(checked)
        }
        Item { Layout.fillWidth: true }
        Button {
            text: "Arıza sıfırla"
            onClicked: App.resetFault()
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Label { text: "Hedef hız"; color: Theme.muted }
        Slider {
            id: slider
            from: -4000
            to: 4000
            stepSize: 50
            value: 1500
            Layout.fillWidth: true
        }
        SpinBox {
            id: spin
            from: -6000
            to: 6000
            stepSize: 50
            editable: true
            value: slider.value
            onValueModified: slider.value = value
            implicitWidth: 96
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Repeater {
            model: [0, 1000, 2500, -1500]
            delegate: Button {
                required property int modelData
                text: modelData
                flat: true
                implicitWidth: 52
                onClicked: { slider.value = modelData; App.setSetpoint(modelData) }
            }
        }
        Item { Layout.fillWidth: true }
        Button {
            text: "Gönder"
            highlighted: true
            onClicked: App.setSetpoint(spin.value)
        }
    }
}
