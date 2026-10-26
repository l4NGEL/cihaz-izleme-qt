"""Benzeticinin protokol tarafı için birim testleri (python -m unittest sim/test_sim.py).

Kontrol değerleri C++ çekirdek testleriyle aynıdır; iki uygulama aynı baytları
üretmek zorunda.
"""

import random
import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import msd200_sim as sim  # noqa: E402


def rtu(adu: bytes) -> bytes:
    return adu + struct.pack("<H", sim.crc16_modbus(adu))


class Protokol(unittest.TestCase):
    def test_crc_kontrol_degerleri(self):
        self.assertEqual(sim.crc16_modbus(b"123456789"), 0x4B37)
        self.assertEqual(sim.crc16_ccitt(b"123456789"), 0x29B1)

    def test_cerceve_bicimi(self):
        f = sim.encode_frame(0x10, 0, b"AB")
        self.assertEqual(f[:7], bytes([0xA5, 0x5A, 2, 0x10, 0, 0x41, 0x42]))
        self.assertEqual(struct.unpack("<H", f[7:])[0], sim.crc16_ccitt(f[2:7]))

    def test_bilinen_modbus_istegi(self):
        self.assertEqual(rtu(bytes([1, 3, 0, 0, 0, 10])), bytes.fromhex("01030000000AC5CD"))


class ModbusIstasyonu(unittest.TestCase):
    def setUp(self):
        self.motor = sim.Motor()
        self.slave = sim.ModbusSlave(self.motor, 1, False, 0, 0.0, random.Random(0))

    def test_input_okuma(self):
        self.motor.speed = -300.4
        (resp,) = self.slave.feed(rtu(bytes([1, 4, 0, 0, 0, 6])))
        self.assertEqual(resp[:3], bytes([1, 4, 12]))
        self.assertEqual(struct.unpack(">h", resp[3:5])[0], -300)
        self.assertEqual(sim.crc16_modbus(resp[:-2]), struct.unpack("<H", resp[-2:])[0])

    def test_hedef_yazma_ve_sinir_disi(self):
        req = rtu(bytes([1, 6, 0, 0]) + struct.pack(">h", -1500))
        self.assertEqual(self.slave.feed(req), [req])  # yanıt = yankı
        self.assertEqual(self.motor.setpoint, -1500)
        (resp,) = self.slave.feed(rtu(bytes([1, 6, 0, 0]) + struct.pack(">H", 7000)))
        self.assertEqual(resp[1:3], bytes([0x86, 3]))

    def test_gecersiz_adres_ve_fonksiyon(self):
        (resp,) = self.slave.feed(rtu(bytes([1, 3, 0, 5, 0, 1])))
        self.assertEqual(resp[1:3], bytes([0x83, 2]))
        (resp,) = self.slave.feed(rtu(bytes([1, 0x2B])))
        self.assertEqual(resp[1:3], bytes([0xAB, 1]))

    def test_crc_hatali_istek_yanitsiz_ve_senkron_korunur(self):
        bad = bytearray(rtu(bytes([1, 4, 0, 0, 0, 1])))
        bad[-1] ^= 0xFF
        good = rtu(bytes([1, 4, 0, 0, 0, 1]))
        out = self.slave.feed(bytes(bad) + good)
        self.assertEqual(len(out), 1)

    def test_baska_istasyona_yanit_verilmez(self):
        self.assertEqual(self.slave.feed(rtu(bytes([2, 4, 0, 0, 0, 1]))), [])

    def test_modbus_tcp(self):
        slave = sim.ModbusSlave(self.motor, 1, True, 0, 0.0, random.Random(0))
        req = struct.pack(">HHHB", 0x1234, 0, 6, 1) + bytes([3, 0, 0, 0, 1])
        (resp,) = slave.feed(req)
        self.assertEqual(resp[:9], struct.pack(">HHHB", 0x1234, 0, 5, 1) + bytes([3, 2]))

    def test_kontrol_ve_ariza(self):
        self.motor.write_control(sim.CTRL_ENABLE)
        self.assertTrue(self.motor.enabled)
        self.motor.temp = 85
        self.motor.step(0.002, random.Random(0))
        self.assertEqual(self.motor.fault, sim.FAULT_OVERTEMP)
        self.assertFalse(self.motor.enabled)
        self.motor.write_control(sim.CTRL_FAULT_RESET)  # hâlâ sıcak
        self.assertEqual(self.motor.fault, sim.FAULT_OVERTEMP)
        self.motor.temp = 60
        self.motor.write_control(sim.CTRL_FAULT_RESET | sim.CTRL_ENABLE)
        self.assertEqual(self.motor.fault, sim.FAULT_NONE)
        self.assertTrue(self.motor.enabled)


class MotorModeli(unittest.TestCase):
    def test_rampa_hedefe_ulasir(self):
        m = sim.Motor()
        m.write_control(sim.CTRL_ENABLE)
        m.setpoint = 1000
        rng = random.Random(0)
        for _ in range(250):  # 0.5 s @ 2000 rpm/s -> 1000 rpm
            m.step(0.002, rng)
        self.assertAlmostEqual(m.speed, 1000.0, places=6)

    def test_yuksuz_isinma_dengede_kalir(self):
        m = sim.Motor()
        m.write_control(sim.CTRL_ENABLE)
        m.setpoint = 3000
        rng = random.Random(0)
        for _ in range(int(300 / 0.01)):  # 5 dakika
            m.step(0.01, rng)
        self.assertEqual(m.fault, sim.FAULT_NONE)
        self.assertLess(m.temp, 60)

    def test_yuk_asiri_isitir(self):
        m = sim.Motor()
        m.load = 6.0
        m.write_control(sim.CTRL_ENABLE)
        m.setpoint = 3000
        rng = random.Random(0)
        for _ in range(int(120 / 0.01)):
            m.step(0.01, rng)
        self.assertEqual(m.fault, sim.FAULT_OVERTEMP)


if __name__ == "__main__":
    unittest.main()
