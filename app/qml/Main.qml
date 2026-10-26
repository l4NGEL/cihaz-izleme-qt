import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cihaz 1.0
import "Theme.js" as Theme

ApplicationWindow {
    id: win
    width: 1440
    height: 880
    minimumWidth: 1100
    minimumHeight: 700
    visible: true
    title: "Cihaz İzleme — MSD-200 motor sürücüsü"
    color: Theme.bg

    palette.window: Theme.panel
    palette.windowText: Theme.text
    palette.base: Theme.panelAlt
    palette.alternateBase: Theme.panel
    palette.text: Theme.text
    palette.button: Theme.panelAlt
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: "#ffffff"
    palette.mid: Theme.border
    palette.dark: Theme.border
    palette.light: Theme.panelAlt
    palette.placeholderText: Theme.muted

    property real plotWindow: 20

    header: ConnectionBar {}

    RowLayout {
        anchors.fill: parent
        anchors.margins: Theme.gap
        spacing: Theme.gap

        ChannelPanel {
            Layout.preferredWidth: 290
            Layout.fillHeight: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: Theme.gap

            RowLayout {
                Layout.fillWidth: true
                Label {
                    text: "Canlı grafikler"
                    color: Theme.text
                    font.pixelSize: 15
                    font.bold: true
                }
                Item { Layout.fillWidth: true }
                Label { text: "Pencere"; color: Theme.muted }
                Repeater {
                    model: [10, 20, 60, 120]
                    delegate: Button {
                        required property int modelData
                        text: modelData + " sn"
                        flat: win.plotWindow !== modelData
                        highlighted: win.plotWindow === modelData
                        onClicked: win.plotWindow = modelData
                    }
                }
            }

            PlotCard {
                title: "Hız (rpm)"
                unit: "rpm"
                window: win.plotWindow
                channels: [
                    { key: "uart.hiz", name: "UART" },
                    { key: "mb.hiz", name: "Modbus" },
                    { key: "can.hiz", name: "CAN" },
                    { key: "mb.hedef", name: "Hedef", dashed: true }
                ]
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            PlotCard {
                title: "Akım (A)"
                unit: "A"
                window: win.plotWindow
                channels: [
                    { key: "uart.akim", name: "UART" },
                    { key: "mb.akim", name: "Modbus" },
                    { key: "can.akim", name: "CAN" }
                ]
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
            PlotCard {
                title: "Sıcaklık (°C)"
                unit: "°C"
                window: win.plotWindow
                channels: [
                    { key: "uart.sicaklik", name: "UART" },
                    { key: "mb.sicaklik", name: "Modbus" },
                    { key: "can.sicaklik", name: "CAN" }
                ]
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }

        ColumnLayout {
            Layout.preferredWidth: 340
            Layout.fillHeight: true
            spacing: Theme.gap

            ControlPanel { Layout.fillWidth: true }
            StatsPanel { Layout.fillWidth: true }
            EventPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }
    }

    footer: Rectangle {
        height: 28
        color: Theme.panel
        border.color: Theme.border
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 18
            Rectangle {
                width: 10; height: 10; radius: 5
                color: !App.running ? Theme.muted : (App.replaying ? Theme.can : Theme.ok)
            }
            Label { text: App.statusText; color: Theme.text }
            Label {
                visible: App.running
                text: "t = " + Number(App.stats.sure || 0).toFixed(1) + " s"
                color: Theme.muted
                font.family: Theme.mono
            }
            Label {
                visible: App.replaying
                text: "oynatma %" + Math.round((App.stats.oynatmaIlerleme || 0) * 100)
                color: Theme.can
            }
            Item { Layout.fillWidth: true }
            Label {
                visible: App.recording
                text: "● KAYIT  " + Number(App.stats.kayitSayisi || 0) + " kayıt · "
                      + (Number(App.stats.kayitBayt || 0) / 1048576).toFixed(2) + " MB"
                color: Theme.err
                font.bold: true
            }
            Label {
                visible: App.exportProgress >= 0
                text: "CSV %" + Math.round(App.exportProgress * 100)
                color: Theme.accent
            }
        }
    }
}
