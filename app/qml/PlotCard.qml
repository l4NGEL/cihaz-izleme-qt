import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtCharts
import Cihaz 1.0
import "Theme.js" as Theme

// Bir büyüklüğün (hız, akım...) üç hattan gelen değerlerini üst üste çizer.
// Seriler açılışta channels listesinden oluşturulur; veriyi PlotFeeder basar.
Card {
    id: root
    property string unit
    property real window: 20
    property var channels: []

    headerExtra: [
        Label {
            text: feeder.pointCount + " nokta"
            color: Theme.muted
            font.pixelSize: 11
        }
    ]

    ChartView {
        id: chart
        Layout.fillWidth: true
        Layout.fillHeight: true
        Layout.minimumHeight: 150
        antialiasing: true
        backgroundColor: "transparent"
        plotAreaColor: Theme.bg
        legend.alignment: Qt.AlignTop
        legend.labelColor: Theme.text
        legend.font.pixelSize: 11
        margins.top: 0
        margins.bottom: 0
        margins.left: 0
        margins.right: 0

        ValueAxis {
            id: axisX
            min: feeder.xMin
            max: feeder.xMax
            tickCount: 6
            labelFormat: "%.0f s"
            labelsColor: Theme.muted
            gridLineColor: Theme.border
            color: Theme.border
            labelsFont.pixelSize: 10
        }
        ValueAxis {
            id: axisY
            min: feeder.yMin
            max: feeder.yMax
            tickCount: 5
            labelFormat: "%.1f"
            labelsColor: Theme.muted
            gridLineColor: Theme.border
            color: Theme.border
            labelsFont.pixelSize: 10
        }
    }

    PlotFeeder {
        id: feeder
        windowSeconds: root.window
        maxPoints: Math.max(200, Math.round(chart.plotArea.width * 2))
    }

    Component.onCompleted: {
        for (const c of channels) {
            const id = App.channelId(c.key)
            const s = chart.createSeries(ChartView.SeriesTypeLine, c.name, axisX, axisY)
            const source = c.key.startsWith("uart") ? "UART" : c.key.startsWith("mb") ? "Modbus" : "CAN"
            s.color = c.dashed ? Theme.ok : Theme.sourceColor(source)
            s.width = c.dashed ? 1.2 : 1.6
            if (c.dashed) s.style = Qt.DashLine
            s.useOpenGL = false
            feeder.addSeries(s, id)
        }
    }
}
