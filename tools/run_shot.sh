#!/bin/bash
# 1ショット分: オシロをアーム → 指令 → トリガー待ち → RAW取得 → 解析。
# usage: tools/run_shot.sh <width_ms> <label> [出力ディレクトリ] [先行パルス列(ms,カンマ区切り)]
#   例: tools/run_shot.sh 8 A01            # 0MPa付近から8msを1発
#       tools/run_shot.sh 8 A02 out 8,8    # 8ms x2 で圧力を上げた直後に、本番の8msを撮る
# 出力: <dir>/<label>_w<width>.json.gz(波形), .log(シリアルログ), .json(解析結果)
# ポートを開くとボードがリセットされるため、1ショットに10秒ほどかかる(P2の圧力は変わらない)。
set -u
W=${1:?width_ms}
LABEL=${2:?label}
OUT=${3:-shots}
PRE=${4:-}
mkdir -p "$OUT"
HERE="$(cd "$(dirname "$0")" && pwd)"
VP=~/Documents/VSCode/visa-mcp/.venv/bin/python
SP=~/.platformio/penv/bin/python
BASE="$OUT/${LABEL}_w${W}"

# CH2は 1V/div・オフセット-2.5V(0.5〜4.5V付近を画面内に収め、4.07Vでのクリップを避ける)
ARM="$VP $HERE/scope_capture.py arm --ch2-scale 1 --ch2-offset -2.5"
if [ -n "$PRE" ]; then
  # 先行パルスで圧力を上げた直後(下流から抜ける前)にオシロをアームし、本番の1発だけを撮る
  $SP "$HERE/send_pulse.py" "$W" --pre "$PRE" --arm-cmd "$ARM 2>&1 | grep -v Warn" --log "$BASE.log" \
    || { echo "指令が受理されませんでした"; exit 2; }
else
  $ARM 2>&1 | grep -v Warn || exit 1
  $SP "$HERE/send_pulse.py" "$W" --log "$BASE.log" || { echo "指令が受理されませんでした"; exit 2; }
fi
$VP "$HERE/scope_capture.py" wait --timeout 5 2>&1 | grep -v Warn || { echo "オシロがトリガーされませんでした"; exit 3; }
$VP "$HERE/scope_capture.py" read "$BASE.json.gz" 2>&1 | grep -v Warn
P2B=$(grep -o 'p2_before=[-0-9.]*' "$BASE.log" | tail -1 | cut -d= -f2)
python3 "$HERE/analyze_pulse.py" "$BASE.json.gz" --cmd-ms "$W" ${P2B:+--p2-before "$P2B"} > "$BASE.json"
python3 - "$BASE.json" <<'EOF'
import json, sys
r = json.load(open(sys.argv[1]))
keys = ["p2_before_fw", "open_width_ms", "deadtime_ms", "baseline_mpa", "final_mpa", "dP_net_mpa",
        "peak_mpa", "dP_peak_mpa", "settle_pm0p01mpa_ms", "settle_pm10pct_final_ms",
        "final_drift_v", "ch2_clipped_samples", "error"]
print({k: r[k] for k in keys if k in r})
EOF
find "$HERE" -name __pycache__ -exec rm -rf {} + 2>/dev/null
