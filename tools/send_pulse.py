#!/usr/bin/env python3
"""特性測定ビルド(env:m5stack-core2-characterize)に単発パルス "P<ms>" を指令し、[TEST]/[PULSE]/[VALVE]ログを取る。

pyserial が必要なので PlatformIO の仮想環境で実行する:
  ~/.platformio/penv/bin/python tools/send_pulse.py 8 --log shot.log
  # 運用点付近から撮る: 先に 8ms を2発打って圧力を上げ、直後にオシロをアームして本番の8msを撮る
  ~/.platformio/penv/bin/python tools/send_pulse.py 8 --pre 8,8 --arm-cmd "<オシロをアームするコマンド>"

ポートを開くとボードがリセットされることがある(DTR/RTSを立てない設定でも発生を確認)。
そのためポートを開いた直後の出力を見て、リセットされていればNORMAL状態に遷移するまで待ってから指令する。

--pre は、下流から圧力が抜ける前(数秒以内)に2次側を目標の開始圧まで上げるための先行パルス列(カンマ区切りのms)。
先行パルスは1発ごとに NORMAL に戻るのを待ってから次を打つ。全部打ち終えたあと --arm-cmd(オシロをアーム
するシェルコマンド)を実行し、その直後に本番の width_ms を指令する(オシロは本番の1発だけを撮る)。
--arm-cmd なしなら、オシロは事前に `scope_capture.py arm` でアームしておくこと。
"""
import argparse
import subprocess
import sys
import time

import serial

LOG_PREFIXES = ("[TEST]", "[PULSE]", "[VALVE]", "[STATE]", "[FAULT]")


def read_until(ser, marker, timeout, sink):
    """markerを含む行が来るまで(または timeout 秒)読む。読んだログ行は sink に追加。見つかれば True。"""
    t_end = time.time() + timeout
    while time.time() < t_end:
        raw = ser.readline()
        if not raw:
            continue
        line = raw.decode(errors="replace").rstrip()
        if line.startswith(LOG_PREFIXES):
            sink.append(line)
        if marker in line:
            return True
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("width_ms", type=float, help="本番パルス幅(ms)。小数可(例 3.6 → U3600 でマイクロ秒指定)")
    ap.add_argument("--port", default="/dev/cu.usbserial-556F0043041")
    ap.add_argument("--listen", type=float, default=2.5, help="本番パルス指令後にログを聞く秒数")
    ap.add_argument("--log", default=None, help="受信ログの保存先")
    ap.add_argument("--pre", default="", help="先行パルス列(ms, カンマ区切り。例 8,8)")
    ap.add_argument("--arm-cmd", default=None, help="先行パルス後・本番前に実行するシェルコマンド(オシロのアーム)")
    args = ap.parse_args()

    ser = serial.Serial()
    ser.port = args.port
    ser.baudrate = 115200
    ser.timeout = 0.1
    ser.dtr = False
    ser.rts = False
    ser.open()

    # 開いた直後の出力を見て、リセットされていれば起動完了とセンサ初期化(Init→Normal)を待つ
    saw_reset = False
    t_end = time.time() + 1.5
    while time.time() < t_end:
        raw = ser.readline()
        if raw and b"rst:" in raw:
            saw_reset = True
    if saw_reset:
        # リセット直後はINIT/FAULTから始まり、センサが有効になってNORMALに遷移するまで指令は拒否される
        read_until(ser, "-> NORMAL", 20, [])
        time.sleep(0.3)
        print("[send_pulse] board was reset on open; waited until NORMAL")
    ser.reset_input_buffer()

    pre_lines = []
    for w in [int(x) for x in args.pre.split(",") if x.strip()]:
        ser.write(f"P{w}\n".encode())
        if not read_until(ser, "COOLDOWN -> NORMAL", 3.0, pre_lines):
            print(f"[send_pulse] pre-pulse P{w} did not complete (rejected?)")
            print("\n".join(pre_lines))
            ser.close()
            sys.exit(1)
    if args.pre:
        print("[send_pulse] pre-pulses done: "
              + " | ".join(l for l in pre_lines if "COOLDOWN -> NORMAL" in l))

    if args.arm_cmd:
        subprocess.run(args.arm_cmd, shell=True, check=True)
    ser.reset_input_buffer()
    ser.write(f"U{round(args.width_ms * 1000)}\n".encode())
    main_lines = []
    t_end = time.time() + args.listen
    while time.time() < t_end:
        raw = ser.readline()
        if raw:
            line = raw.decode(errors="replace").rstrip()
            if line.startswith(LOG_PREFIXES):
                print(line)
                main_lines.append(line)
    ser.close()
    if args.log:
        with open(args.log, "w") as f:
            f.write("\n".join(pre_lines + main_lines) + "\n")
    accepted = any("accepted" in l for l in main_lines)
    sys.exit(0 if accepted else 1)


if __name__ == "__main__":
    main()
