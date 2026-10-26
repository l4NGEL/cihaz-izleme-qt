import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cihaz 1.0
import "Theme.js" as Theme

// Tüm kanalların anlık değeri, kaynağa göre gruplu. Son 1 sn'de güncellenmeyen
// kanal soluk gösterilir: hangi hattın sustuğu bir bakışta görülsün.
Card {
    title: "Kanallar"

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: App.channels
        spacing: 1
        section.property: "source"
        section.delegate: Rectangle {
            required property string section
            width: list.width
            height: 26
            color: "transparent"
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Rectangle {
                    width: 8; height: 8; radius: 4
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.sourceColor(section)
                }
                Label {
                    text: section
                    color: Theme.muted
                    font.pixelSize: 11
                    font.bold: true
                }
            }
        }
        delegate: Rectangle {
            id: row
            required property string label
            required property string text
            required property string unit
            required property bool fresh
            width: list.width
            height: 26
            radius: 3
            color: fresh ? Theme.panelAlt : "transparent"
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 8
                Label {
                    text: row.label
                    color: row.fresh ? Theme.text : Theme.muted
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Label {
                    text: row.text
                    color: row.fresh ? Theme.text : Theme.muted
                    font.family: Theme.mono
                    horizontalAlignment: Text.AlignRight
                }
                Label {
                    text: row.unit
                    color: Theme.muted
                    Layout.preferredWidth: 30
                }
            }
        }
    }
}
