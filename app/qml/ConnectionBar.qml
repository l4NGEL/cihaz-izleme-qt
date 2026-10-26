import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Cihaz 1.0
import "Theme.js" as Theme

// Bağlantı tanımları, bağlan/kes, kayıt ve tekrar oynatma
ToolBar {
    id: bar
    background: Rectangle { color: Theme.panel; border.color: Theme.border }

    component Field: ColumnLayout {
        property alias label: lbl.text
        default property alias content: holder.data
        spacing: 2
        Label { id: lbl; color: Theme.muted; font.pixelSize: 11 }
        Item {
            id: holder
            implicitWidth: childrenRect.width
            implicitHeight: childrenRect.height
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 4
        anchors.bottomMargin: 6
        spacing: 10

        Field {
            label: "UART telemetri"
            ComboBox {
                id: telemetry
                property bool ready: false
                width: 230
                editable: true
                enabled: !App.running
                model: App.serialPorts().concat(["tcp:127.0.0.1:5001"])
                // Model atanınca ilk öğe seçilir; kayıtlı tanımı ezmesin diye hazır olana kadar yazma
                Component.onCompleted: { editText = App.telemetrySpec; ready = true }
                onEditTextChanged: if (ready) App.telemetrySpec = editText
                onActivated: App.telemetrySpec = currentText
                ToolTip.visible: hovered
                ToolTip.text: "serial:COM3?baud=115200 · serial:/dev/ttyUSB0 · tcp:host:port · udp:port · boş = yok"
            }
        }
        Field {
            label: "Modbus (RS-485 / TCP)"
            Row {
                spacing: 6
                TextField {
                    width: 190
                    enabled: !App.running
                    text: App.modbusSpec
                    placeholderText: "serial:COM4?baud=19200&parity=even"
                    onEditingFinished: App.modbusSpec = text
                }
                ComboBox {
                    width: 70
                    enabled: !App.running
                    model: ["RTU", "TCP"]
                    currentIndex: App.modbusTcp ? 1 : 0
                    onActivated: App.modbusTcp = currentIndex === 1
                }
            }
        }
        Field {
            label: "CAN (eklenti:arayüz)"
            TextField {
                width: 140
                enabled: !App.running
                text: App.canSpec
                placeholderText: "socketcan:vcan0"
                onEditingFinished: App.canSpec = text
                ToolTip.visible: hovered
                ToolTip.text: "Yüklü eklentiler: " + App.canPlugins().join(", ")
            }
        }

        Button {
            text: App.running ? "Bağlantıyı kes" : "Bağlan"
            highlighted: !App.running
            Layout.alignment: Qt.AlignBottom
            onClicked: App.running ? App.disconnectDevice() : App.connectDevice()
        }

        Item { Layout.fillWidth: true }

        Button {
            text: App.recording ? "■ Kaydı durdur" : "● Kayda başla"
            enabled: App.running && !App.replaying
            Layout.alignment: Qt.AlignBottom
            palette.buttonText: App.recording ? Theme.err : Theme.text
            onClicked: App.toggleRecording()
            ToolTip.visible: hovered
            ToolTip.text: "Ham veri " + App.logDirectory() + " altına .cil olarak yazılır"
        }
        Field {
            label: "Oynatma hızı"
            ComboBox {
                id: speed
                width: 64
                model: ["x1", "x2", "x5", "x10"]
            }
        }
        Button {
            text: "Kaydı oynat…"
            enabled: !App.running
            Layout.alignment: Qt.AlignBottom
            onClicked: { openDialog.exporting = false; openDialog.open() }
        }
        Button {
            text: "CSV…"
            enabled: App.exportProgress < 0
            Layout.alignment: Qt.AlignBottom
            onClicked: { openDialog.exporting = true; openDialog.open() }
        }
    }

    FileDialog {
        id: openDialog
        property bool exporting: false
        title: exporting ? "CSV'ye aktarılacak kayıt" : "Oynatılacak kayıt"
        nameFilters: ["Kayıt dosyası (*.cil)", "Tümü (*)"]
        currentFolder: App.logDirectoryUrl()
        onAccepted: {
            if (exporting)
                App.exportCsv(selectedFile)
            else
                App.startReplay(selectedFile, [1, 2, 5, 10][speed.currentIndex])
        }
    }
}
