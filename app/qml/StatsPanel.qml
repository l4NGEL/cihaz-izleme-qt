import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Cihaz 1.0
import "Theme.js" as Theme

// Hat sağlığı sayaçları (edinim iş parçacığından 4 Hz'de gelir)
Card {
    id: card
    title: "Hat sağlığı"
    readonly property var s: App.stats

    component Stat: RowLayout {
        property string name
        property var value
        property bool bad: false
        Layout.fillWidth: true
        Label { text: name; color: Theme.muted; Layout.fillWidth: true }
        Label {
            text: value === undefined ? "—" : value
            color: bad ? Theme.err : Theme.text
            font.family: Theme.mono
        }
    }
    component Head: RowLayout {
        property string name
        property color dot
        property bool open
        Layout.topMargin: 4
        Rectangle { width: 8; height: 8; radius: 4; color: open ? dot : Theme.muted }
        Label { text: name; color: Theme.text; font.bold: true; font.pixelSize: 11 }
    }

    Head { name: "UART"; dot: Theme.uart; open: !!card.s.telemetriAcik || !!card.s.oynatma }
    Stat { name: "çerçeve / hız"; value: (card.s.uartCerceve || 0) + "  ·  " + Math.round(card.s.cerceveHizi || 0) + "/s" }
    Stat { name: "CRC hatası"; value: card.s.crcHata; bad: (card.s.crcHata || 0) > 0 }
    Stat { name: "sıra atlaması (kayıp)"; value: card.s.siraAtlama; bad: (card.s.siraAtlama || 0) > 0 }
    Stat { name: "atılan bayt"; value: card.s.atilanBayt }

    Head { name: "Modbus"; dot: Theme.modbus; open: !!card.s.modbusAcik }
    Stat { name: "istek / yanıt"; value: (card.s.mbIstek || 0) + " / " + (card.s.mbYanit || 0) }
    Stat { name: "zaman aşımı / yeniden"; value: (card.s.mbZamanAsimi || 0) + " / " + (card.s.mbYenidenDeneme || 0); bad: (card.s.mbZamanAsimi || 0) > 0 }
    Stat { name: "gecikme (son / en kötü)"; value: Number(card.s.mbGecikme || 0).toFixed(1) + " / " + Number(card.s.mbMaxGecikme || 0).toFixed(1) + " ms" }

    Head { name: "CAN"; dot: Theme.can; open: !!card.s.canAcik }
    Stat { name: "çerçeve"; value: card.s.canCerceve }
}
