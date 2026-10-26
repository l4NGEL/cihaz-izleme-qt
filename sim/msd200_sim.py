#!/usr/bin/env python3
"""MSD-200 motor sürücüsü benzetimi.

Gerçek cihaz yokken izleme uygulamasını ve testleri beslemek için sürücünün üç
arayüzünü taklit eder:

  UART   : 500 Hz ikili telemetri (A5 5A | LEN | MSGID | SEQ | yük | CRC16-CCITT)
  RS-485 : Modbus RTU istasyonu (ya da Modbus TCP sunucusu)
  CAN    : 0x181 / 0x281 durum mesajları (100 Hz), 0x701 heartbeat (1 Hz)

Her arayüz şu uçlardan birine bağlanabilir:
  pty            Linux sözde terminal; uygulama /dev/pts/N'i seri port gibi açar
  COM5, /dev/ttyUSB0   gerçek seri port (pyserial gerekir)
  tcp:5001       TCP sunucusu (0 verilirse boş port seçilir); Windows'ta pty yerine
  rtu-tcp:5021   (yalnız --modbus) RTU çerçevesi TCP üzerinden, seri-ethernet dönüştürücü gibi
  vcan0          (yalnız --can) Linux SocketCAN arayüzü

Hazır olunca stdout'a tek satır yazar:  HAZIR uart=... modbus=... can=...
Yalnızca standart kütüphane kullanır (gerçek seri port için pyserial isteğe bağlı).
"""

from __future__ import annotations

import argparse
import math
import os
import random
import socket
import struct
import sys
import threading
import time

# --------------------------------------------------------------------- protokol


def crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def crc16_ccitt(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def encode_frame(msg_id: int, seq: int, payload: bytes) -> bytes:
    body = bytes([len(payload), msg_id, seq & 0xFF]) + payload
    return b"\xA5\x5A" + body + struct.pack("<H", crc16_ccitt(body))


MSG_MOTOR_STATUS = 0x01
MSG_ENCODER = 0x02
MSG_EVENT = 0x10

REG_SETPOINT, REG_CONTROL, REG_RAMP = 0, 1, 2
CTRL_ENABLE, CTRL_FAULT_RESET = 0x0001, 0x0002
FAULT_NONE, FAULT_OVERTEMP = 0, 1
FW_VERSION = 0x0102
ENCODER_CPR = 4096

# ------------------------------------------------------------------ motor modeli


class Motor:
    """Birinci dereceden hız rampası + basit ısıl model."""

    def __init__(self) -> None:
        self.lock = threading.Lock()
        self.setpoint = 0
        self.enabled = False
        self.ramp = 2000.0          # rpm/s
        self.speed = 0.0
        self.accel = 0.0
        self.current = 0.0
        self.temp = 25.0
        self.bus_v = 48.0
        self.fault = FAULT_NONE
        self.position = 0.0         # enkoder sayımı
        self.time_us = 0
        self.load = 0.0             # mil yükünün 3000 rpm'deki ek akımı (A)
        self.events: list[str] = []

    def step(self, dt: float, rng: random.Random) -> None:
        target = float(self.setpoint) if self.enabled and self.fault == FAULT_NONE else 0.0
        max_dv = self.ramp * dt
        dv = max(-max_dv, min(max_dv, target - self.speed))
        self.accel = dv / dt if dt > 0 else 0.0
        self.speed += dv
        self.current = 0.5 + 0.0012 * abs(self.speed) + 0.0015 * abs(self.accel) + self.load * abs(self.speed) / 3000.0
        if not self.enabled and abs(self.speed) < 1:
            self.current = 0.05
        self.current += rng.gauss(0, 0.03)
        # Isınma: I²R kaybı, ortama doğru soğuma (zaman sabiti ~33 s). Yüksüz
        # 3000 rpm'de ~50 °C'de dengelenir; --yuk ile aşırı ısınma arızası denenebilir.
        self.temp += (0.04 * self.current ** 2 - 0.03 * (self.temp - 25.0)) * dt
        self.bus_v = 48.0 - 0.15 * self.current + rng.gauss(0, 0.05)
        self.position += self.speed / 60.0 * ENCODER_CPR * dt
        self.time_us = (self.time_us + int(dt * 1e6)) & 0xFFFFFFFF
        if self.fault == FAULT_NONE and self.temp > 80.0:
            self.fault = FAULT_OVERTEMP
            self.enabled = False
            self.events.append("FAULT OVERTEMP")

    def write_control(self, value: int) -> None:
        if value & CTRL_FAULT_RESET and self.fault != FAULT_NONE:
            if self.temp < 70.0:
                self.fault = FAULT_NONE
                self.events.append("FAULT CLEARED")
            else:
                self.events.append("FAULT RESET REFUSED (HOT)")
        enable = bool(value & CTRL_ENABLE) and self.fault == FAULT_NONE
        if enable != self.enabled:
            self.events.append("ENABLED" if enable else "DISABLED")
        self.enabled = enable

    def input_registers(self) -> list[int]:
        return [
            int(round(self.speed)) & 0xFFFF,
            int(round(self.current * 100)) & 0xFFFF,
            int(round(self.temp * 10)) & 0xFFFF,
            int(round(self.bus_v * 10)) & 0xFFFF,
            self.fault,
            FW_VERSION,
        ]

    def holding_registers(self) -> list[int]:
        return [self.setpoint & 0xFFFF, (CTRL_ENABLE if self.enabled else 0), int(self.ramp)]


# ------------------------------------------------------------------- bayt uçları


class Endpoint:
    name = ""

    def read(self, timeout: float) -> bytes:
        raise NotImplementedError

    def write(self, data: bytes) -> None:
        raise NotImplementedError

    def close(self) -> None:
        pass


class PtyEndpoint(Endpoint):
    def __init__(self, link: str | None) -> None:
        import pty
        import tty

        self.master, slave = pty.openpty()
        tty.setraw(slave)
        self._slave = slave  # açık tutulmazsa istemci kapanınca master EIO verir
        self.name = os.ttyname(slave)
        if link:
            try:
                os.unlink(link)
            except FileNotFoundError:
                pass
            os.symlink(self.name, link)
            self.name = link
        os.set_blocking(self.master, False)

    def read(self, timeout: float) -> bytes:
        import select

        r, _, _ = select.select([self.master], [], [], timeout)
        if not r:
            return b""
        try:
            return os.read(self.master, 4096)
        except (BlockingIOError, OSError):
            return b""

    def write(self, data: bytes) -> None:
        try:
            os.write(self.master, data)
        except (BlockingIOError, OSError):
            pass  # okuyan yoksa pty tamponu dolar; gerçek UART gibi bayt düşer

    def close(self) -> None:
        os.close(self.master)
        os.close(self._slave)


class SerialEndpoint(Endpoint):
    def __init__(self, port: str, baud: int) -> None:
        import serial  # type: ignore

        self.port = serial.Serial(port, baud, timeout=0)
        self.name = port

    def read(self, timeout: float) -> bytes:
        self.port.timeout = timeout
        return self.port.read(4096)

    def write(self, data: bytes) -> None:
        self.port.write(data)

    def close(self) -> None:
        self.port.close()


class TcpServerEndpoint(Endpoint):
    """Tek istemcili TCP sunucusu; istemci düşünce yenisini kabul eder."""

    def __init__(self, port: int) -> None:
        self.srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.srv.bind(("127.0.0.1", port))
        self.srv.listen(1)
        self.srv.settimeout(0)
        self.client: socket.socket | None = None
        self.lock = threading.Lock()
        self.name = f"tcp:127.0.0.1:{self.srv.getsockname()[1]}"

    def _accept(self) -> None:
        if self.client is None:
            try:
                c, _ = self.srv.accept()
                c.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                c.settimeout(0)
                self.client = c
            except (BlockingIOError, OSError):
                pass

    def read(self, timeout: float) -> bytes:
        with self.lock:
            self._accept()
            c = self.client
        if c is None:
            time.sleep(timeout)
            return b""
        import select

        r, _, _ = select.select([c], [], [], timeout)
        if not r:
            return b""
        try:
            data = c.recv(4096)
        except (BlockingIOError, OSError):
            data = b""
        if not data:
            self._drop()
        return data

    def write(self, data: bytes) -> None:
        with self.lock:
            self._accept()
            c = self.client
        if c is None:
            return
        try:
            c.sendall(data)
        except (BlockingIOError, OSError):
            self._drop()

    def _drop(self) -> None:
        with self.lock:
            if self.client is not None:
                self.client.close()
                self.client = None

    def close(self) -> None:
        self._drop()
        self.srv.close()


def open_endpoint(spec: str, link: str | None, baud: int) -> Endpoint:
    if spec == "pty":
        return PtyEndpoint(link)
    if spec.startswith("tcp:"):
        return TcpServerEndpoint(int(spec[4:]))
    if spec.startswith("rtu-tcp:"):
        return TcpServerEndpoint(int(spec[8:]))
    return SerialEndpoint(spec, baud)


# ----------------------------------------------------------------- Modbus istasyonu


class ModbusSlave:
    def __init__(self, motor: Motor, unit: int, tcp: bool, delay_ms: float, drop_rate: float,
                 rng: random.Random) -> None:
        self.motor, self.unit, self.tcp = motor, unit, tcp
        self.delay = delay_ms / 1000.0
        self.drop_rate = drop_rate
        self.rng = rng
        self.buf = b""
        self.served = 0
        self.last_write = 0.0

    def feed(self, data: bytes) -> list[bytes]:
        self.buf += data
        out = []
        while True:
            resp, consumed = self._parse_tcp() if self.tcp else self._parse_rtu()
            if consumed == 0:
                break
            self.buf = self.buf[consumed:]
            if resp is not None:
                if self.rng.random() < self.drop_rate:
                    continue  # yanıt kaybı benzetimi: istemci zaman aşımına düşmeli
                out.append(resp)
        return out

    def _parse_rtu(self) -> tuple[bytes | None, int]:
        b = self.buf
        if len(b) < 2:
            return None, 0
        fc = b[1]
        if fc in (3, 4, 6):
            need = 8
        elif fc == 16:
            if len(b) < 7:
                return None, 0
            need = 9 + b[6]
        else:
            need = 4  # bilinmeyen fonksiyon: adres+fc+CRC varsay
        if len(b) < need:
            return None, 0
        frame = b[:need]
        if crc16_modbus(frame[:-2]) != struct.unpack("<H", frame[-2:])[0]:
            return None, 1  # CRC tutmadı: gerçek cihaz susar; bir bayt kaydırıp yeniden senkronla
        if frame[0] != self.unit:
            return None, need  # başka istasyona
        pdu = self._handle(frame[1:-2])
        adu = bytes([self.unit]) + pdu
        return adu + struct.pack("<H", crc16_modbus(adu)), need

    def _parse_tcp(self) -> tuple[bytes | None, int]:
        b = self.buf
        if len(b) < 7:
            return None, 0
        tid, proto, length = struct.unpack(">HHH", b[:6])
        if len(b) < 6 + length:
            return None, 0
        unit = b[6]
        pdu = self._handle(b[7:6 + length])
        resp = struct.pack(">HHHB", tid, proto, len(pdu) + 1, unit) + pdu
        return resp, 6 + length

    def _handle(self, pdu: bytes) -> bytes:
        fc = pdu[0]

        def exc(code: int) -> bytes:
            return bytes([fc | 0x80, code])

        m = self.motor
        with m.lock:
            if fc in (3, 4):
                addr, count = struct.unpack(">HH", pdu[1:5])
                regs = m.holding_registers() if fc == 3 else m.input_registers()
                if count == 0 or addr + count > len(regs):
                    return exc(2)
                vals = regs[addr:addr + count]
                return bytes([fc, 2 * count]) + struct.pack(f">{count}H", *vals)
            if fc == 6:
                addr, value = struct.unpack(">HH", pdu[1:5])
                err = self._write(addr, value)
                return exc(err) if err else pdu[:5]
            if fc == 16:
                addr, count = struct.unpack(">HH", pdu[1:5])
                values = struct.unpack(f">{count}H", pdu[6:6 + 2 * count])
                for i, v in enumerate(values):
                    err = self._write(addr + i, v)
                    if err:
                        return exc(err)
                return pdu[:5]
        return exc(1)

    def _write(self, addr: int, value: int) -> int:
        m = self.motor
        self.last_write = time.monotonic()
        if addr == REG_SETPOINT:
            signed = value - 0x10000 if value & 0x8000 else value
            if abs(signed) > 6000:
                return 3
            m.setpoint = signed
        elif addr == REG_CONTROL:
            m.write_control(value)
        elif addr == REG_RAMP:
            if not 10 <= value <= 20000:
                return 3
            m.ramp = float(value)
        else:
            return 2
        return 0


# --------------------------------------------------------------------- iş parçacıkları


def telemetry_loop(ep: Endpoint, motor: Motor, rate_hz: float, corrupt: float, stop: threading.Event,
                   rng: random.Random, stats: dict) -> None:
    """Sabit adımlı benzetim; Windows'ta sleep çözünürlüğü kaba olduğu için
    geçen süreye düşen adımlar toplu üretilip tek yazmada gönderilir."""
    dt = 1.0 / rate_hz
    seq = 0
    t_next = time.monotonic()
    while not stop.is_set():
        now = time.monotonic()
        chunk = bytearray()
        while t_next <= now:
            with motor.lock:
                motor.step(dt, rng)
                status = struct.pack("<IfffhH", motor.time_us, motor.speed, motor.current, motor.bus_v,
                                     int(round(motor.temp * 10)), (1 if motor.enabled else 0) | (motor.fault << 8))
                counts = ((int(motor.position) + 2 ** 31) % 2 ** 32) - 2 ** 31  # int32'ye sar
                enc = struct.pack("<Ii", motor.time_us, counts)
                events = motor.events
                motor.events = []
            chunk += encode_frame(MSG_MOTOR_STATUS, seq, status)
            seq = (seq + 1) & 0xFF
            chunk += encode_frame(MSG_ENCODER, seq, enc)
            seq = (seq + 1) & 0xFF
            for e in events:
                chunk += encode_frame(MSG_EVENT, seq, e.encode()[:64])
                seq = (seq + 1) & 0xFF
            stats["frames"] += 2 + len(events)
            t_next += dt
        if chunk and ep is not None:
            if corrupt > 0:
                for i in range(len(chunk)):
                    if rng.random() < corrupt:
                        chunk[i] ^= 1 << rng.randrange(8)
                        stats["corrupted"] += 1
            ep.write(bytes(chunk))
        time.sleep(max(0.0, min(0.002, t_next - time.monotonic())))


def physics_only_loop(motor: Motor, stop: threading.Event, rng: random.Random) -> None:
    dt = 0.002
    t_next = time.monotonic()
    while not stop.is_set():
        now = time.monotonic()
        while t_next <= now:
            with motor.lock:
                motor.step(dt, rng)
                motor.events = []
            t_next += dt
        time.sleep(0.002)


def modbus_loop(ep: Endpoint, slave: ModbusSlave, stop: threading.Event) -> None:
    while not stop.is_set():
        data = ep.read(0.05)
        if not data:
            continue
        for resp in slave.feed(data):
            if slave.delay > 0:
                time.sleep(slave.delay)
            ep.write(resp)
            slave.served += 1


def can_loop(iface: str, motor: Motor, stop: threading.Event, stats: dict) -> None:
    s = socket.socket(socket.AF_CAN, socket.SOCK_RAW, socket.CAN_RAW)  # type: ignore[attr-defined]
    s.bind((iface,))
    fmt = "=IB3x8s"

    def send(can_id: int, data: bytes) -> None:
        s.send(struct.pack(fmt, can_id, len(data), data.ljust(8, b"\x00")))
        stats["can"] += 1

    t_next = time.monotonic()
    hb_next = t_next
    while not stop.is_set():
        now = time.monotonic()
        if now >= t_next:
            with motor.lock:
                speed = max(-32768, min(32767, int(round(motor.speed))))
                cur = max(-32768, min(32767, int(round(motor.current * 100))))
                bus = max(0, min(65535, int(round(motor.bus_v * 10))))
                temp = max(0, min(255, int(round(motor.temp + 40))))
                fault = motor.fault
                state = 1 if motor.enabled else 0
                pos = int(round(((motor.position % ENCODER_CPR) * 360.0 / ENCODER_CPR) * 100)) % 36000
            # 0x181 Intel; 0x281 Motorola (büyük uçlu) yerleşim
            send(0x181, struct.pack("<hhHB", speed, cur, bus, temp) + b"\x00")
            send(0x281, bytes([fault, state]) + struct.pack(">H", pos))
            t_next += 0.01
        if now >= hb_next:
            send(0x701, b"\x05")
            hb_next += 1.0
        time.sleep(0.002)


def auto_profile_loop(motor: Motor, slave: ModbusSlave | None, stop: threading.Event) -> None:
    """Demo için hedef hızı döngüsel değiştirir; bir istemci yazma yapınca durur."""
    profile = [1500, 3000, -1500, 0, 2500, 800]
    i = 0
    with motor.lock:
        motor.write_control(CTRL_ENABLE)
    while not stop.wait(4.0):
        if slave is not None and slave.last_write and time.monotonic() - slave.last_write < 60:
            continue
        with motor.lock:
            motor.setpoint = profile[i % len(profile)]
            if motor.fault == FAULT_NONE and not motor.enabled:
                motor.write_control(CTRL_ENABLE)
        i += 1


# ----------------------------------------------------------------------------- ana


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="MSD-200 motor sürücüsü benzetimi")
    ap.add_argument("--uart", help="telemetri ucu: pty | tcp:PORT | COMx | /dev/ttyX")
    ap.add_argument("--uart-link", help="pty için sabit sembolik bağlantı yolu")
    ap.add_argument("--modbus", help="Modbus ucu: pty | tcp:PORT (Modbus TCP) | rtu-tcp:PORT | COMx")
    ap.add_argument("--modbus-link", help="pty için sabit sembolik bağlantı yolu")
    ap.add_argument("--can", help="SocketCAN arayüzü (Linux), ör. vcan0")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--birim", type=int, default=1, help="Modbus istasyon adresi")
    ap.add_argument("--hiz", type=float, default=500.0, help="telemetri hızı (Hz)")
    ap.add_argument("--hata-orani", type=float, default=0.0, help="UART bayt bozulma olasılığı")
    ap.add_argument("--mb-gecikme", type=float, default=2.0, help="Modbus yanıt gecikmesi (ms)")
    ap.add_argument("--mb-kayip", type=float, default=0.0, help="Modbus yanıtını düşürme olasılığı")
    ap.add_argument("--otomatik", action="store_true", help="hedef hızı kendiliğinden değiştir (demo)")
    ap.add_argument("--yuk", type=float, default=0.0, help="3000 rpm'de ek yük akımı (A); ~6 aşırı ısıtır")
    ap.add_argument("--sure", type=float, default=0.0, help="saniye sonra çık (0: Ctrl+C'ye kadar)")
    ap.add_argument("--tohum", type=int, default=1)
    args = ap.parse_args(argv)

    if not (args.uart or args.modbus or args.can):
        ap.error("en az bir arayüz gerekli (--uart, --modbus, --can)")

    rng = random.Random(args.tohum)
    motor = Motor()
    motor.load = args.yuk
    stop = threading.Event()
    stats = {"frames": 0, "corrupted": 0, "can": 0}
    threads: list[threading.Thread] = []
    endpoints: list[Endpoint] = []
    ready = ["HAZIR"]

    if args.uart:
        ep = open_endpoint(args.uart, args.uart_link, args.baud)
        endpoints.append(ep)
        ready.append(f"uart={ep.name}")
        threads.append(threading.Thread(target=telemetry_loop,
                                        args=(ep, motor, args.hiz, args.hata_orani, stop, random.Random(args.tohum + 1),
                                              stats), daemon=True))
    else:
        threads.append(threading.Thread(target=physics_only_loop, args=(motor, stop, random.Random(args.tohum + 1)),
                                        daemon=True))

    slave = None
    if args.modbus:
        ep = open_endpoint(args.modbus, args.modbus_link, args.baud)
        endpoints.append(ep)
        tcp = args.modbus.startswith("tcp:")
        slave = ModbusSlave(motor, args.birim, tcp, args.mb_gecikme, args.mb_kayip, random.Random(args.tohum + 2))
        ready.append(f"modbus={ep.name}" + (" (Modbus TCP)" if tcp else ""))
        threads.append(threading.Thread(target=modbus_loop, args=(ep, slave, stop), daemon=True))

    if args.can:
        ready.append(f"can={args.can}")
        threads.append(threading.Thread(target=can_loop, args=(args.can, motor, stop, stats), daemon=True))

    if args.otomatik:
        threads.append(threading.Thread(target=auto_profile_loop, args=(motor, slave, stop), daemon=True))

    for t in threads:
        t.start()
    print(" ".join(ready), flush=True)

    try:
        if args.sure > 0:
            stop.wait(args.sure)
        else:
            while not stop.wait(1.0):
                pass
    except KeyboardInterrupt:
        pass
    stop.set()
    for t in threads:
        t.join(timeout=1.0)
    for ep in endpoints:
        ep.close()
    served = slave.served if slave else 0
    print(f"BITTI cerceve={stats['frames']} bozulan_bayt={stats['corrupted']} modbus_yanit={served} "
          f"can={stats['can']}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
