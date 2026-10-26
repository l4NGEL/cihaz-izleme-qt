import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cihaz 1.0
import "Theme.js" as Theme

// Olaylar (bağlantı, komut, cihaz arızası) + kayda düşülen kullanıcı notu
Card {
    title: "Olaylar"

    ListView {
        id: list
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        model: App.events
        spacing: 2
        delegate: RowLayout {
            id: entry
            required property string time
            required property string text
            required property int level
            width: list.width
            spacing: 8
            Label {
                text: entry.time
                color: Theme.muted
                font.family: Theme.mono
                font.pixelSize: 11
                Layout.alignment: Qt.AlignTop
            }
            Label {
                text: entry.text
                color: entry.level === 2 ? Theme.err : entry.level === 1 ? Theme.warn : Theme.text
                wrapMode: Text.Wrap
                font.pixelSize: 12
                Layout.fillWidth: true
            }
        }
    }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            id: note
            Layout.fillWidth: true
            placeholderText: "Kayda not düş (ör. yük eklendi)"
            enabled: App.running
            onAccepted: { App.addNote(text); text = "" }
        }
    }
}
