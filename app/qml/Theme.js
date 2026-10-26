.pragma library

// Koyu, düşük parlaklıklı arayüz: saha/test tezgâhında uzun süre bakılacak ekran
var bg = "#0f141a"
var panel = "#161d26"
var panelAlt = "#1c2530"
var border = "#283340"
var text = "#d6dde5"
var muted = "#8593a3"
var accent = "#3ea6ff"
var ok = "#3fb950"
var warn = "#d29922"
var err = "#f85149"

// Kaynak renkleri: aynı büyüklük üç hattan geldiğinde grafikte ayırt edilsin
var uart = "#3ea6ff"
var modbus = "#f0883e"
var can = "#b38cff"

function sourceColor(source) {
    if (source === "UART") return uart
    if (source === "Modbus") return modbus
    return can
}

var mono = "monospace"
var radius = 6
var gap = 10
