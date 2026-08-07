# Chibi-T_Furoshiki_AutoAirAdjust

M5Stack Core2 用の空気圧自動調整コントローラー ファームウェア（PlatformIO / Arduino フレームワーク）です。  
拡張基板 [Chibi-T_Furoshiki_AutoAirAdjust_PCB](https://github.com/todateman/Chibi-T_Furoshiki_AutoAirAdjust_PCB) を搭載した M5Stack Core2 で、1次側・2次側の空気圧センサと燃料圧力センサの値を監視し、ソレノイドバルブの開閉によって1次側から2次側への圧縮空気の流れを自動制御します。

## 概要

- 対応ホスト: M5Stack Core2（MBUS 30pin コネクタで拡張基板と直結）
- 1次側空気圧: 参考監視用（充填時想定 0.6MPa）
- 2次側空気圧・燃料圧力: 目標値 0.35MPa（本体ボタンで0.01MPa単位で調整可能。詳細は[目標燃圧の調整（ボタン操作）](#目標燃圧の調整ボタン操作)を参照）
- 燃料圧力は2次側空気圧と1:1で連動するため、**燃料圧力センサの値を最終制御量**とし、目標燃圧±0.01MPaに収まるようノーマリークローズのソレノイドバルブを固定パルス幅のON/OFF制御で駆動します。

拡張基板のハードウェア仕様（コネクタ・回路構成）は別リポジトリの `Chibi-T_Furoshiki_AutoAirAdjust_PCB` の README を参照してください。

1次側・2次側空気圧・燃圧の値は、BLE Peripheral機能により [M5NanoC6_BLE_Central](https://github.com/todateman/M5NanoC6_BLE_Central)（BLE Central、M5Stack Basicへの中継用ブリッジ）へリアルタイム送信されます。

## センサ・アクチュエータ構成

| 項目 | デバイス | I2Cアドレス / 接続 | 備考 |
| --- | --- | --- | --- |
| 1次側空気圧センサ | DFRobot Gravity MPX5700AP (SEN0456) | `0x16`<BR>（Grove I2C, J5） | 参考監視のみ |
| 2次側空気圧センサ | DFRobot Gravity MPX5700AP (SEN0456) | `0x17`<BR>（Grove I2C, J6） | 安全判定に使用 |
| 燃圧センサ | アナログ 0.5–4.5V / 0–1.0MPa | ADS1015 `0x48` の AIN0 | 制御量（最終判定に使用） |
| ソレノイドバルブ | DC12V、ノーマリークローズ | `SOLENOID_PIN`<BR>（M5Core2 - GPIO19<BR>M5Stack Basic - GPIO13） | 固定パルス幅ON/OFF制御 |

## 制御ロジック

状態機械: `Init → Normal ⇄ PulseOpen → Cooldown → Normal`、および異常検知時は任意状態から `Fault` へ即遷移します。

- **Normal**: 燃圧が目標帯下限（目標燃圧−0.01MPa）未満なら固定幅（`PULSE_WIDTH_MS`）だけバルブを開き `PulseOpen` へ。目標帯内、または上限（目標燃圧＋0.01MPa）超過時はバルブ閉のまま待機（能動排気機構は無いため、上限超過時は自然消費を待ちます）。目標燃圧はボタン操作で実行時に変更可能（[目標燃圧の調整（ボタン操作）](#目標燃圧の調整ボタン操作)を参照）。
- **PulseOpen → Cooldown**: パルス終了後、`PULSE_COOLDOWN_MS` だけ休止してから再判定（配管内圧力の伝播待ち・センサ再安定待ち・コイルのデューティ抑制のため）。
- **Fault**: 以下を優先度順に判定し、検知したらバルブを強制的に閉じます（ヒステリシス・デバウンス付き）。
  1. センサ異常（I2C通信エラー・レンジ外の値・燃圧センサはADC読み取り値のばらつきによる未接続検出も含む）
  2. 過圧（燃圧 or 2次側が0.50MPa超過）
  3. 1次側供給不足（1次側が0.40MPa未満）
  4. 目標帯への復帰タイムアウト（5秒以上パルスを打ち続けても改善しない＝配管漏れ・弁固着等の多重防御）

  センサ異常時は、警告表示およびシリアルログに `(P1 P2)` のように原因センサ（P1=1次側 / P2=2次側 / FUEL=燃圧）が付記され、どのセンサが異常かを個別に識別できます。

主要な制御定数（パルス幅・休止時間・しきい値など）はすべて [`src/config.h`](src/config.h) に集約されており、調整する場合はここを編集してください。

## ファイル構成

```text
src/
├── config.h            # ピン番号・I2Cアドレス・全制御定数（BLE UUID含む）
├── system_types.h      # 状態機械の型定義（SystemState, FaultReason, SensorReadings等）
├── pressure_sensors.h/.cpp  # 3センサの初期化・読み取り・kPa/MPa変換・異常検知
├── valve_driver.h/.cpp      # ソレノイドの非ブロッキング固定パルス駆動
├── controller.h/.cpp        # 状態機械・安全保護ロジック
├── display_ui.h/.cpp        # M5Core2 LCDへのリアルタイム表示
├── ble_service.h/.cpp       # BLE PeripheralによるセンサデータNotify送信
├── fuel_target_store.h/.cpp # 目標燃圧の実行時保持・ボタン調整・NVS(Preferences)への永続化
└── main.cpp                 # setup()/loop()、各モジュールの統合とシリアルログ出力
```

## 目標燃圧の調整（ボタン操作）

目標燃圧（初期値0.35MPa）は、本体のA/B/Cボタンで実機上から調整・保存できます。

| ボタン | 操作 | 動作 |
| --- | --- | --- |
| A | 短押し | 目標燃圧を `-0.01MPa` |
| C | 短押し | 目標燃圧を `+0.01MPa` |
| B | 長押し（0.5秒以上） | 現在の目標燃圧をNVS（ESP32 Preferences、EEPROM相当の不揮発領域）へ保存 |

- A/Cボタンでの調整は即座に制御（バルブ駆動判定）とLCD表示に反映されますが、**Bボタンで長押し保存するまでは電源を切ると失われ**、次回起動時は最後に保存した値（未保存なら初期値0.35MPa）に戻ります。
- 調整可能範囲は `0.10〜0.45MPa`（`FUEL_TARGET_MIN_MPA`〜`FUEL_TARGET_MAX_MPA`）に制限されており、これを超えて調整することはできません。過圧しきい値（`OVERPRESSURE_TRIP_MPA` = 0.50MPa）に対して安全マージンを確保しています。
- LCD画面のFuelゲージ行に現在の目標燃圧が `TGT:0.35` のように表示され、保存前は末尾に `*` が付いて未保存であることを示します（保存後は消えます）。
- 調整・保存操作はシリアルログにも `[SETTINGS] fuel target -> 0.36MPa` / `[SETTINGS] fuel target 0.36MPa SAVED to NVS` の形式で出力されます。
- 各定数は [`src/config.h`](src/config.h) に集約されているため、範囲やステップ幅を変更する場合はここを編集してください。

## BLE通信（M5NanoC6への中継）

本体はBLE Peripheral（Server）として動作し、[M5NanoC6_BLE_Central](https://github.com/todateman/M5NanoC6_BLE_Central)（BLE Central）に対して1次側/2次側空気圧・燃圧をNotifyで送信します。  
M5NanoC6はさらにI2C経由でM5Stack Basicへ中継します。

- デバイス名: `ChibiT-AutoAirAdjust`
- 送信周期: `DISPLAY_UPDATE_INTERVAL_MS`（100ms）ごと。BLEクライアントが接続している場合のみ送信
- 送信形式: `PRI:<P1のMPa値>\nSEC:<P2のMPa値>\nFUEL:<燃圧のMPa値>\n` を1回のNotifyでまとめて送信  
  （例: `PRI:0.85\nSEC:0.72\nFUEL:2.10\n`）
- センサ異常時（`SensorSample.valid == false`）は、直近の有効値を送り続けます  
  （安全制御は本体側の `Controller` が独立して担保するため、BLE Notifyは監視データの中継に徹します）

| 用途 | UUID |
| --- | --- |
| Service UUID | `7c44181A-c1a4-4635-a119-b490ed272552`（M5NanoC6側の実装に合わせた固定値） |
| 接続維持用ダミーCharacteristic（Read/Write、実データは扱わない） | `c9f878f1-c311-4452-ae5e-e813b4fe057d` |
| センサ値Notify用Characteristic | `1d25ec49-e19c-4bb6-8c36-5dc8d8aaaebe` |

UUID・デバイス名・送信周期を変更する場合は [`src/config.h`](src/config.h) の `BLE_*` 定数を編集してください。

## 使用ライブラリ（`platformio.ini` の `lib_deps`）

- [`m5stack/M5Unified`](https://github.com/m5stack/M5Unified) — Core2本体制御・LCD表示
- [`adafruit/Adafruit ADS1X15`](https://github.com/adafruit/Adafruit_ADS1X15) — 燃圧センサ用ADC(ADS1015)読み取り
- [`DFRobot/DFRobot_MPX5700`](https://github.com/DFRobot/DFRobot_MPX5700) — 1次側/2次側空気圧センサ読み取り

## ビルド・書き込み

PlatformIO CLI（または VSCode の PlatformIO 拡張機能）を使用します。

```sh
pio run -e m5stack-core2          # ビルド
pio run -e m5stack-core2 -t upload  # 書き込み
pio device monitor -b 115200        # シリアルモニタ
```

## 動作確認

1. `pio run -e m5stack-core2` でビルドが通ることを確認。
2. 実機書き込み後、シリアルログ（115200bps）で起動時のセンサ疎通結果（`[BOOT] PressureSensors.begin() -> ...`）を確認。
3. 定常運転中は状態遷移（`[STATE] ...`）、バルブの開閉（`[VALVE] OPEN/CLOSE t=...`）、異常発生・解消（`[FAULT] ...`）がエッジトリガでログ出力されるので、パルス幅・クールダウンが設計値通りに動作しているか確認する。
4. LCD画面には1次側（P1）・2次側（P2）・燃圧（Fuel）がそれぞれ大きな数値とゲージバーでリアルタイム表示され、下部にバルブ状態、フォルト発生時のみ警告表示（赤背景）が表示される。  
   各ゲージの表示色はそのセンサ自身の有効性・しきい値（P1: `PRIMARY_SUPPLY_LOW_TRIP_MPA`、P2: `OVERPRESSURE_TRIP_MPA`、Fuel: 目標帯）との比較のみで決まり、他センサの異常やFault遷移による影響は受けない。
5. BLE通信は、起動時にシリアルログで `[BLE] advertising started` を確認。  
   M5NanoC6等のBLE Centralが接続すると `[BLE] client connected`、切断すると `[BLE] client disconnected, restarting advertising` が出力される。  
   nRF Connect等のBLEスキャナアプリでも、デバイス名 `ChibiT-AutoAirAdjust` へ接続し、Notify Characteristic（`1d25ec49-...`）を購読することで送信データを直接確認できる。
6. デバッグ用に、BLE接続の有無によらず `DISPLAY_UPDATE_INTERVAL_MS`（100ms）ごとに `[BLE TX] PRI=... SEC=... FUEL=... (connected=yes/no)` がUSB Serialへ出力される。BLE未接続でも送信予定データと接続状態をシリアルモニタだけで確認できる。
7. Aボタン/Cボタンを押すたびにLCDの `TGT:` 表示が0.01MPaずつ増減し、末尾に未保存を示す `*` が付くこと、`0.10〜0.45MPa` の範囲で頭打ちになることを確認する。Bボタンを長押しするとシリアルログに `[SETTINGS] ... SAVED to NVS` が出力され、`*` が消えることを確認する。保存後に電源を入れ直し、調整した目標燃圧が復元されることを確認する。

## ライセンス

MIT License（詳細は [`LICENSE`](LICENSE) を参照）
