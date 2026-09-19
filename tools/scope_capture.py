#!/usr/bin/env python3
"""RIGOL DHO804 の単発トリガー設定・アーム・RAW(1M点)取得。

pyvisa が必要なので visa-mcp の仮想環境で実行する:
  ~/Documents/VSCode/visa-mcp/.venv/bin/python tools/scope_capture.py arm
  ~/Documents/VSCode/visa-mcp/.venv/bin/python tools/scope_capture.py wait
  ~/Documents/VSCode/visa-mcp/.venv/bin/python tools/scope_capture.py read out.json.gz

チャネル割り当て(docs/oscilloscope_dho804_visa_mcp.md 参照):
  CH1 = ソレノイド12V駆動ライン(ローサイドスイッチ: LOW=開弁、平常時 約15V)
  CH2 = 2次側空気圧センサのアナログ出力 V0
トリガーは CH1 立ち下がり(=開弁)。RAW取得の時間軸はトリガー点を0とする。
"""
import argparse
import gzip
import json
import sys
import time

import pyvisa

RESOURCE = "USB0::6833::1101::DHO8A253701207::0::INSTR"
CHUNK = 125000


def open_scope():
    rm = pyvisa.ResourceManager()
    inst = rm.open_resource(RESOURCE)
    inst.timeout = 60000
    inst.write_termination = "\n"
    inst.read_termination = "\n"
    return inst


def q(inst, cmd):
    return inst.query(cmd).strip()


def cmd_arm(inst, args):
    inst.write(f":TIMebase:MAIN:SCALe {args.tb}")
    inst.write(":TRIGger:MODE EDGE")
    inst.write(":TRIGger:EDGE:SOURce CHAN1")
    inst.write(":TRIGger:EDGE:SLOPe NEGative")
    inst.write(f":TRIGger:EDGE:LEVel {args.level}")
    if args.ch2_scale is not None:
        inst.write(f":CHANnel2:SCALe {args.ch2_scale}")
    if args.ch2_offset is not None:
        inst.write(f":CHANnel2:OFFSet {args.ch2_offset}")
    inst.write(":SINGle")
    time.sleep(0.3)
    print("armed status:", q(inst, ":TRIGger:STATus?"))


def cmd_wait(inst, args):
    t0 = time.time()
    while time.time() - t0 < args.timeout:
        st = q(inst, ":TRIGger:STATus?")
        if st == "STOP":
            print("STOP")
            return 0
        time.sleep(0.2)
    print("TIMEOUT (status=%s)" % st)
    return 1


def fetch_channel(inst, ch):
    inst.write(f":WAVeform:SOURce CHANnel{ch}")
    inst.write(":WAVeform:MODE RAW")
    inst.write(":WAVeform:FORMat WORD")
    # 前回のSTARt/STOPが残っているとプリアンブルの点数が切れるため、毎回全範囲に戻す
    inst.write(":WAVeform:STARt 1")
    inst.write(":WAVeform:STOP 1000000")
    pre = q(inst, ":WAVeform:PREamble?").split(",")
    n = int(pre[2])
    vals = []
    s = 1
    while s <= n:
        e = min(s + CHUNK - 1, n)
        inst.write(f":WAVeform:STARt {s}")
        inst.write(f":WAVeform:STOP {e}")
        vals += inst.query_binary_values(
            ":WAVeform:DATA?", datatype="H", is_big_endian=False, container=list)
        s = e + 1
    inst.write(":WAVeform:STARt 1")
    inst.write(":WAVeform:STOP 1000000")
    return {
        "n": len(vals), "xinc": float(pre[4]), "xorg": float(pre[5]),
        "yinc": float(pre[7]), "yorg": float(pre[8]), "yref": float(pre[9]),
        "raw": vals,
    }


def cmd_read(inst, args):
    meta = {
        "idn": q(inst, "*IDN?"),
        "tb_scale": q(inst, ":TIMebase:MAIN:SCALe?"),
        "trig_level": q(inst, ":TRIGger:EDGE:LEVel?"),
        "trig_slope": q(inst, ":TRIGger:EDGE:SLOPe?"),
        "ch1_probe": q(inst, ":CHANnel1:PROBe?"), "ch2_probe": q(inst, ":CHANnel2:PROBe?"),
        "ch2_scale": q(inst, ":CHANnel2:SCALe?"), "ch2_offset": q(inst, ":CHANnel2:OFFSet?"),
        "captured_at": time.strftime("%Y-%m-%dT%H:%M:%S"),
    }
    data = {"meta": meta, "ch1": fetch_channel(inst, 1), "ch2": fetch_channel(inst, 2)}
    with gzip.open(args.out, "wt") as f:
        json.dump(data, f)
    print("saved", args.out, "n=", data["ch1"]["n"], data["ch2"]["n"])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("arm", help="単発トリガー(CH1立ち下がり)を設定してアーム")
    a.add_argument("--tb", type=float, default=0.01, help="タイムベース s/div (既定 0.01)")
    a.add_argument("--level", type=float, default=7.8, help="トリガーレベル V (既定 7.8)")
    a.add_argument("--ch2-scale", type=float, default=None)
    a.add_argument("--ch2-offset", type=float, default=None)
    w = sub.add_parser("wait", help="トリガー成立(STOP)まで待つ")
    w.add_argument("--timeout", type=float, default=10.0)
    r = sub.add_parser("read", help="RAW波形(CH1/CH2, 1M点)を取得して .json.gz に保存")
    r.add_argument("out")
    args = ap.parse_args()

    inst = open_scope()
    try:
        rc = {"arm": cmd_arm, "wait": cmd_wait, "read": cmd_read}[args.cmd](inst, args)
    finally:
        inst.close()
    sys.exit(rc or 0)


if __name__ == "__main__":
    main()
