#!/usr/bin/env python3
"""scope_capture.py read で保存した1パルス分の波形から、バルブ特性を算出する(標準ライブラリのみ)。

usage: python3 tools/analyze_pulse.py shot.json.gz [--cmd-ms 8] [--p2-before 0.320]

時間軸はトリガー点(CH1立ち下がり=開弁)を0とする。
  開弁時間   : CH1 が平常電圧の半分を下回っている区間の長さ
  デッドタイム: 開弁から、CH2(0.1ms平均)がベースライン+max(10mV, 8σ)を超えるまで
  正味ΔP     : 最終値(取得窓の末尾10ms平均) - ベースライン
  整定時間   : 閉弁後、最終値の±0.01MPa帯(および±10%)から最後に外れた時刻 - 閉弁
圧力換算は MPX5700AP 公称(Vs=5V): kPa = (V-0.2)/(4.5/700)、ゲージ = 絶対 - 101.3kPa。
"""
import argparse
import gzip
import json
import statistics as st

ATM_KPA = 101.3


def kpa(v):
    return (v - 0.2) / (4.5 / 700.0)


def gauge_mpa(v):
    return (kpa(v) - ATM_KPA) / 1000.0


def to_volts(c):
    return [(r - c["yorg"] - c["yref"]) * c["yinc"] for r in c["raw"]]


def block_means(x, w):
    """幅wの単純移動平均(中心)。累積和で高速に計算する。"""
    n = len(x)
    c = [0.0]
    for a in x:
        c.append(c[-1] + a)
    h = w // 2
    out = []
    for i in range(n):
        lo, hi = max(0, i - h), min(n, i + h + 1)
        out.append((c[hi] - c[lo]) / (hi - lo))
    return out


def analyze(path, cmd_ms=None, p2_before=None):
    with gzip.open(path, "rt") as f:
        d = json.load(f)
    v1, v2 = to_volts(d["ch1"]), to_volts(d["ch2"])
    dt, t0 = d["ch1"]["xinc"], d["ch1"]["xorg"]
    n = len(v1)
    tm = lambda i: (t0 + i * dt) * 1e3  # ms
    idx = lambda ms: int(round((ms / 1e3 - t0) / dt))
    res = {"file": path, "cmd_ms": cmd_ms, "p2_before_fw": p2_before, "window_ms": [tm(0), tm(n - 1)]}

    # --- CH1: 開弁・閉弁エッジ ---
    s1 = block_means(v1, max(3, int(1.6e-6 / dt)))
    idle = st.median(s1[: max(1, idx(-1))]) if idx(-1) > 100 else st.median(s1[:1000])
    th = idle / 2
    edges = [(tm(i), "fall" if s1[i] < th else "rise")
             for i in range(1, n) if (s1[i - 1] >= th) != (s1[i] >= th)]
    res["ch1_idle_v"] = round(idle, 2)
    res["ch1_edges"] = [(round(t, 4), k) for t, k in edges[:6]]
    fall = next((t for t, k in edges if k == "fall"), None)
    rise = next((t for t, k in edges if k == "rise" and fall is not None and t > fall), None)
    if fall is None or rise is None:
        res["error"] = "open/close edge not found"
        return res
    res["t_open_ms"], res["t_close_ms"] = round(fall, 4), round(rise, 4)
    res["open_width_ms"] = round(rise - fall, 3)
    if cmd_ms:
        res["width_error_ms"] = round(res["open_width_ms"] - cmd_ms, 3)

    # --- CH2: ベースライン・デッドタイム・ピーク・最終値・整定 ---
    blk = max(1, int(1e-4 / dt))  # 0.1ms
    s2 = block_means(v2, blk)
    pre_lo, pre_hi = idx(tm(0) + 0.05), idx(fall - 0.5)
    if pre_hi - pre_lo < 1000:
        res["error"] = "pre-trigger window too short for baseline"
        return res
    base = st.mean(v2[pre_lo:pre_hi])
    sd = st.pstdev(s2[pre_lo:pre_hi])
    thr = base + max(0.010, 8 * sd)
    i_open = idx(fall)
    dead = next((tm(i) - fall for i in range(i_open, n) if s2[i] > thr), None)
    res["baseline_v"] = round(base, 4)
    res["baseline_mpa"] = round(gauge_mpa(base), 4)
    res["deadtime_ms"] = round(dead, 3) if dead is not None else None

    i_close = idx(rise)
    win = int(10e-3 / dt)  # 末尾10msを最終値とし、その直前10msとの差(drift)で整定済みか確認する
    fin = st.mean(v2[max(i_close, n - win):])
    prev = st.mean(v2[max(i_close, n - 2 * win): n - win])
    res["final_v"] = round(fin, 4)
    res["final_mpa"] = round(gauge_mpa(fin), 4)
    res["final_drift_v"] = round(fin - prev, 4)  # 末尾10msと直前10msの差。大きければ未整定
    res["dP_net_mpa"] = round((kpa(fin) - kpa(base)) / 1000.0, 4)

    ip = max(range(i_open, n), key=lambda i: s2[i])
    res["peak_v"] = round(s2[ip], 4)
    res["peak_mpa"] = round(gauge_mpa(s2[ip]), 4)
    res["peak_t_after_close_ms"] = round(tm(ip) - rise, 2)
    res["dP_peak_mpa"] = round((kpa(s2[ip]) - kpa(base)) / 1000.0, 4)
    res["overshoot_ratio"] = round((s2[ip] - base) / (fin - base), 2) if fin - base > 0.005 else None

    def settle_ms(half_v):
        last = None
        for i in range(ip, n):
            if abs(s2[i] - fin) > half_v:
                last = i
        return round(tm(last) - rise, 1) if last is not None else 0.0

    res["settle_pm0p01mpa_ms"] = settle_ms(0.01 * 1000 * 4.5 / 700)
    res["settle_pm10pct_final_ms"] = settle_ms(0.10 * fin)
    res["ch2_clipped_samples"] = sum(1 for x in v2 if x > 4.06)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("path")
    ap.add_argument("--cmd-ms", type=float, default=None)
    ap.add_argument("--p2-before", type=float, default=None)
    a = ap.parse_args()
    print(json.dumps(analyze(a.path, a.cmd_ms, a.p2_before), ensure_ascii=False, indent=1))


if __name__ == "__main__":
    main()
