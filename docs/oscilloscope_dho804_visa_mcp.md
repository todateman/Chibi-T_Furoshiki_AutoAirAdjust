# RIGOL DHO804 実機計測メモ（VISA-MCP経由）

[README.md「今後実施すべき実機検証」](../README.md#今後実施すべき実機検証)で必要となる、ソレノイドバルブのデッドタイム計測・圧力伝播遅延の計測を、Claude Code上の`VISA-MCP`経由でRIGOL DHO804を操作しながら進めるための接続情報・動作確認結果をまとめる。  
波形サンプリング（`:WAVeform:...`系コマンド）自体はまだ未実施で、ここまでで確認できているのは「SCPIコマンドで機器を操作できる状態にあること」まで。

## 前提: VISA-MCPサーバー

- サーバー本体: <https://github.com/todateman/visa-mcp/blob/main/src/server.py> （本プロジェクトとは別リポジトリ）
  ローカルには既にMSPサーバを立ち上げ済みで、`python3 -m visa_mcp.server` で起動可能である前提。
- Claude Code（本プロジェクトの `.mcp.json`）からMCPツールとして `mcp__visa-mcp__list_visa_resources` / `mcp__visa-mcp__send_visa_command` が使える。
- 内部的には `pyvisa.ResourceManager()` を素通しで呼んでいるだけの薄いラッパー（DLL自動検出、`@py`固定ではない）。

## 接続方式: USB（USBTMC）

DHO804は **USB接続**。  
LAN接続ではない（`server.py` 冒頭コメントに「オシロスコープは10.10.0.3」という古い記述が残っているが、現状は不使用・無視してよい）。

USBTMCリソースID（VID::PID::シリアル番号）:

```bash
6833::1101::DHO8A253701207::0
```

- `6833` = `0x1AB1`（RIGOL TECHNOLOGIES の USB Vendor ID）
- `1101` = `0x044D`（DHO800シリーズの USB Product ID）
- `DHO8A253701207` = 今回使用した実機のシリアル番号
- 末尾の `::0` はインターフェース番号

別の個体・別のPCで使う場合は `*IDN?` 応答やOS側のUSBデバイス情報からVID/PID/シリアルを確認し直すこと。

## `send_visa_command` の呼び出し方

`send_visa_command(host, resource_id, command, protocol=...)` は本来LAN機器向け（`host`必須）のツールだが、USB接続の場合は `host` は使われないため、ダミー値でよい。  
`protocol="usb"` を指定すると `USB0::{resource_id}::INSTR` というVISAリソース文字列が組み立てられる。

```python
await send_visa_command(
    host="unused",
    resource_id="6833::1101::DHO8A253701207::0",
    command="*IDN?",
    protocol="usb",
)
```

応答例:

```bash
RIGOL TECHNOLOGIES,DHO804,DHO8A253701207,00.01.02
```

## 動作確認済みSCPIコマンド

以下はすべて2026-08-10時点で実機に対して送信し、期待通り応答・反映されることを確認済み。

| コマンド | 内容 | 確認結果 |
| --- | --- | --- |
| `*IDN?` | 機器識別情報の取得 | `RIGOL TECHNOLOGIES,DHO804,DHO8A253701207,00.01.02` |
| `:TIMebase:MAIN:SCALe?` | タイムベース（水平軸）取得 | 初期値 `1.5E-2`（15ms/div） |
| `:TIMebase:MAIN:OFFSet?` | タイムベースのオフセット取得 | `0.000000` |
| `:TIMebase:MAIN:SCALe 0.001` | タイムベースを1ms/divに設定 | 書き込み `OK`、読み戻し `1.0E-3` で反映確認 |
| `:TRIGger:MODE?` | トリガーモード取得 | `EDGE` |
| `:TRIGger:EDGE:SOURce?` | トリガーソース取得 | 初期値 `CHAN3` |
| `:TRIGger:EDGE:SOURce CHAN1` | トリガーソースをCH1に設定 | 書き込み `OK`、読み戻し `CHAN1` で反映確認 |
| `:TRIGger:EDGE:SLOPe?` | トリガースロープ取得 | 初期値 `NEG` |
| `:TRIGger:EDGE:SLOPe POSitive` | 立ち上がりエッジに設定 | 書き込み `OK`、読み戻し `POS` で反映確認 |
| `:TRIGger:EDGE:LEVel?` | トリガーレベル取得 | 初期値 `0.000000` |
| `:TRIGger:EDGE:LEVel 1.65` | トリガーレベルを1.65V（ESP32の3.3Vロジック中間電圧）に設定 | 書き込み `OK`、読み戻し `1.65E0` で反映確認 |

書き込み系コマンド（`?`を含まないコマンド）は `send_visa_command` 内部で `instr.write()` が呼ばれ、成功すると常に文字列 `"OK"` が返る。  
クエリ（`?`を含むコマンド）は `instr.query()` の戻り値がそのまま返る。

## 既知の問題: `list_visa_resources` が失敗する

`mcp__visa-mcp__list_visa_resources`（`rm.list_resources()` の呼び出し）は毎回下記エラーで失敗し、リソース自動列挙ができない。

```bash
Error listing VISA resources: [Errno 48] Address already in use
```

- `ps aux | grep visa-mcp` で確認したところ、`visa-mcp/src/server.py` のプロセスが複数（5個前後）重複起動していた。ディスカバリー処理（TCPIP/VXI-11検出など）でのポート競合が疑われるが、根本原因は未特定。
- **回避策**: USBTMCリソースIDが分かっていれば、`list_visa_resources` を経由せず `send_visa_command` に直接リソースIDを渡せば通信できる（上記の通り動作確認済み）。  
  リソースIDが不明な機体を新たに使う場合のみ、このエラーの解消（サーバー再起動・重複プロセスの整理など）が必要になる。

## 次フェーズ（未実施）: 波形サンプリング

今回はコマンドでの機器操作確認までで、駆動信号・アナログ波形の実サンプリングは行っていない。  
README記載の計測を行う際は、概ね以下の流れになる想定。

1. CH1にESP32の `SOLENOID_PIN` 駆動信号、CH2にバルブ通過直後の空気圧センサのアナログ電圧（`Gravity: MPX5700AP Air Pressure Sensor` の `V0` ピン）を接続。
2. 上記で確認したトリガー設定（CH1・立ち上がりエッジ・1.65V）を基準に、実際の波形が正しくトリガーされるかを目視 or `:TRIGger:STATus?` 等で確認。
3. `:WAVeform:SOURce`, `:WAVeform:MODE`, `:WAVeform:FORMat`, `:WAVeform:DATA?` 等のコマンド（今回は未検証）で波形データを取得し、デッドタイム・整定時間を算出する。
4. 計測結果を `src/config.h` の `PULSE_WIDTH_MIN_MS` / `PULSE_COOLDOWN_MS` 等へ反映する（README「今後実施すべき実機検証」参照）。
