#!/usr/bin/env python3
"""Uçtan uca test: Python cihaz benzeticisi + cihaz-cli, gerçek süreçler arası.

Senaryolar:
  tcp      UART ve Modbus TCP, hedef hız yazma, kayıt -> oynatma -> CSV (her platform)
  hatalar  bozuk UART baytları ve düşen Modbus yanıtları altında toparlanma (her platform)
  pty      UART + Modbus RTU sözde terminal üzerinden (yalnız Linux)
  can      SocketCAN vcan0 (yalnız Linux, arayüz varsa)
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

FAILURES: list[str] = []


def check(cond: bool, msg: str) -> None:
    print(("  tamam  " if cond else "  HATA   ") + msg, flush=True)
    if not cond:
        FAILURES.append(msg)


class Sim:
    def __init__(self, sim_path: str, *args: str) -> None:
        self.proc = subprocess.Popen([sys.executable, "-u", sim_path, *args], stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT, text=True, encoding="utf-8")
        line = self.proc.stdout.readline().strip()
        if not line.startswith("HAZIR"):
            self.proc.kill()
            raise RuntimeError(f"benzetici başlamadı: {line!r}")
        self.endpoints = dict(re.findall(r"(\w+)=(\S+)", line))
        print(f"  benzetici: {line}", flush=True)

    def stop(self) -> None:
        self.proc.terminate()
        try:
            self.proc.wait(5)
        except subprocess.TimeoutExpired:
            self.proc.kill()


def run_cli(cli: str, *args: str, timeout: float = 60) -> dict:
    out = subprocess.run([cli, *args, "--json", "-q"], capture_output=True, text=True, encoding="utf-8",
                         timeout=timeout)
    if out.returncode != 0:
        print(out.stdout, out.stderr)
        raise RuntimeError(f"cihaz-cli çıkış kodu {out.returncode}")
    return json.loads(out.stdout)


def near(a: float | None, b: float, tol: float) -> bool:
    return a is not None and abs(a - b) <= tol


def scenario_tcp(cli: str, sim: str, tmp: Path) -> None:
    print("[tcp] UART + Modbus TCP, hedef hız, kayıt/oynatma/CSV", flush=True)
    s = Sim(sim, "--uart", "tcp:0", "--modbus", "tcp:0")
    try:
        log = tmp / "tcp.cil"
        r = run_cli(cli, "-t", s.endpoints["uart"], "-m", s.endpoints["modbus"], "--modbus-tcp",
                    "--hedef", "1200", "--sure", "3", "--kayit", str(log))
    finally:
        s.stop()
    v = r["son_deger"]
    check(r["uart_cerceve"] >= 1500, f"UART çerçeve sayısı {r['uart_cerceve']} >= 1500")
    check(r["crc_hata"] == 0 and r["sira_atlama"] == 0, "temiz hatta CRC hatası / sıra atlaması yok")
    check(r["mb_yanit"] >= 20, f"Modbus yanıt sayısı {r['mb_yanit']} >= 20")
    check(r["mb_zaman_asimi"] == 0, f"Modbus zaman aşımı {r['mb_zaman_asimi']} == 0")
    check(near(v.get("mb.hedef"), 1200, 0), f"cihazın hedef register'ı 1200 ({v.get('mb.hedef')})")
    check(near(v.get("uart.hiz"), 1200, 5), f"UART hız 1200'e oturdu ({v.get('uart.hiz')})")
    check(near(v.get("mb.hiz"), 1200, 5), f"Modbus hız 1200'e oturdu ({v.get('mb.hiz')})")
    check(log.exists() and r["kayit_sayisi"] > 100, f"kayıt dosyası yazıldı ({r['kayit_sayisi']} kayıt)")

    t0 = time.monotonic()
    p = run_cli(cli, "--oynat", str(log), "--hiz", "10")
    dt = time.monotonic() - t0
    check(p["uart_cerceve"] == r["uart_cerceve"],
          f"oynatma aynı çerçeveleri üretti ({p['uart_cerceve']} == {r['uart_cerceve']})")
    check(near(p["son_deger"].get("uart.hiz"), v["uart.hiz"], 1e-6), "oynatmanın son hız değeri canlıyla aynı")
    check(dt < 3.0, f"x10 oynatma {dt:.2f} sn sürdü (< 3)")

    csv = tmp / "tcp.csv"
    out = subprocess.run([cli, "--csv", str(log), "--cikti", str(csv)], capture_output=True, text=True,
                         encoding="utf-8")
    check(out.returncode == 0 and csv.exists(), "CSV dışa aktarıldı")
    lines = csv.read_text(encoding="utf-8").splitlines()
    check(lines[0] == "t_s,kaynak,kanal,deger,birim", "CSV başlığı")
    check(sum(1 for line in lines if ",uart.hiz," in line) == r["uart_cerceve"] // 2,
          "CSV'de her MotorStatus çerçevesi için bir hız satırı")


def scenario_faults(cli: str, sim: str, tmp: Path) -> None:
    print("[hatalar] bozuk UART baytları + %10 Modbus yanıt kaybı", flush=True)
    s = Sim(sim, "--uart", "tcp:0", "--modbus", "tcp:0", "--hata-orani", "0.0005", "--mb-kayip", "0.1")
    try:
        r = run_cli(cli, "-t", s.endpoints["uart"], "-m", s.endpoints["modbus"], "--modbus-tcp", "--sure", "4")
    finally:
        s.stop()
    check(r["crc_hata"] > 0, f"CRC hataları sayıldı ({r['crc_hata']})")
    check(r["uart_cerceve"] >= 2000, f"bozulmaya rağmen akış sürdü ({r['uart_cerceve']} çerçeve)")
    check(r["mb_zaman_asimi"] > 0 and r["mb_yeniden_deneme"] > 0,
          f"kayıp yanıtlar zaman aşımı + yeniden deneme ile kapandı ({r['mb_zaman_asimi']} / {r['mb_yeniden_deneme']})")
    check(r["mb_yanit"] >= 15, f"Modbus yanıtları gelmeye devam etti ({r['mb_yanit']})")


def scenario_pty(cli: str, sim: str, tmp: Path) -> None:
    print("[pty] UART + Modbus RTU sözde terminal üzerinden", flush=True)
    s = Sim(sim, "--uart", "pty", "--modbus", "pty")
    try:
        r = run_cli(cli, "-t", f"serial:{s.endpoints['uart']}?baud=115200",
                    "-m", f"serial:{s.endpoints['modbus']}?baud=19200&parity=even", "--hedef", "-800", "--sure", "3")
    finally:
        s.stop()
    v = r["son_deger"]
    check(r["uart_cerceve"] >= 1500 and r["crc_hata"] == 0, f"pty UART akışı ({r['uart_cerceve']} çerçeve)")
    check(r["mb_yanit"] >= 20 and r["mb_zaman_asimi"] == 0, f"Modbus RTU ({r['mb_yanit']} yanıt)")
    check(near(v.get("mb.hiz"), -800, 5), f"RTU üzerinden hedef -800 uygulandı ({v.get('mb.hiz')})")


def scenario_can(cli: str, sim: str, tmp: Path) -> None:
    print("[can] SocketCAN vcan0", flush=True)
    s = Sim(sim, "--can", "vcan0", "--otomatik")
    try:
        r = run_cli(cli, "--can", "socketcan:vcan0", "--sure", "3")
    finally:
        s.stop()
    v = r["son_deger"]
    check(r["can_cerceve"] >= 400, f"CAN çerçeveleri ({r['can_cerceve']})")
    check("can.hiz" in v and "can.pozisyon" in v, "Intel (0x181) ve Motorola (0x281) sinyalleri çözüldü")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--cli", required=True)
    ap.add_argument("--sim", required=True)
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as d:
        tmp = Path(d)
        scenario_tcp(args.cli, args.sim, tmp)
        scenario_faults(args.cli, args.sim, tmp)
        if sys.platform.startswith("linux"):
            scenario_pty(args.cli, args.sim, tmp)
            if Path("/sys/class/net/vcan0").exists():
                scenario_can(args.cli, args.sim, tmp)
            else:
                print("[can] atlandı: vcan0 yok (ip link add dev vcan0 type vcan)")
        else:
            print("[pty] [can] atlandı: yalnız Linux")

    if FAILURES:
        print(f"\n{len(FAILURES)} kontrol başarısız")
        return 1
    print("\ntüm kontroller geçti")
    return 0


if __name__ == "__main__":
    sys.exit(main())
