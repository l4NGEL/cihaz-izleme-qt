import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "Theme.js" as Theme

// Başlıklı panel
Rectangle {
    id: card
    property string title
    property alias headerExtra: extra.data
    default property alias content: body.data

    color: Theme.panel
    radius: Theme.radius
    border.color: Theme.border
    implicitHeight: column.implicitHeight + 20

    ColumnLayout {
        id: column
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: card.title
                color: Theme.text
                font.bold: true
                font.pixelSize: 13
            }
            Item { Layout.fillWidth: true }
            Row { id: extra; spacing: 6 }
        }
        ColumnLayout {
            id: body
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 6
        }
    }
}
