# Chibi-T_Furoshiki_AutoAirAdjust

M5Stack Core2 用の空気圧自動調整コントローラー ファームウェア（PlatformIO / Arduino フレームワーク）です。  
拡張基板 [Chibi-T_Furoshiki_AutoAirAdjust_PCB](https://github.com/todateman/Chibi-T_Furoshiki_AutoAirAdjust_PCB) を搭載した M5Stack Core2 で、1次側・2次側の空気圧センサと燃料圧力センサの値を監視し、ソレノイドバルブの開閉によって1次側から2次側への圧縮空気の流れを自動制御します。

## 概要

- 対応ホスト: M5Stack Core2（MBUS 30pin コネクタで拡張基板と直結）
- 1次側空気圧: 参考監視用（充填時想定 0.6MPa）
- 2次側空気圧: 目標値 0.35MPa（本体ボタンで0.01MPa単位で調整可能。詳細は[目標2次側空気圧の調整（ボタン操作）](#目標2次側空気圧の調整ボタン操作)を参照）
- **2次側空気圧センサの値を最終制御量**とし、目標2次側空気圧±0.01MPaに収まるようノーマリークローズのソレノイドバルブを自己適応パルス幅のON/OFF制御で駆動します。  
  1回のパルスで供給過多にならないよう、パルス幅は最小値から始めて過供給/供給不足の結果に応じて自動的に伸縮します。  
  燃料圧力は2次側空気圧と1:1で連動する前提のため、燃圧センサは制御には使わず、過圧判定・センサ異常判定の多重防御用の監視値として扱います。

拡張基板のハードウェア仕様（コネクタ・回路構成）は別リポジトリの `Chibi-T_Furoshiki_AutoAirAdjust_PCB` の README を参照してください。

1次側・2次側空気圧・燃圧の値は、BLE Peripheral機能により [M5NanoC6_BLE_Central](https://github.com/todateman/M5NanoC6_BLE_Central)（BLE Central、M5Stack Basicへの中継用ブリッジ）へリアルタイム送信されます。

## センサ・アクチュエータ構成

| 項目 | デバイス | I2Cアドレス / 接続 | 備考 |
| --- | --- | --- | --- |
| 1次側空気圧センサ | DFRobot Gravity MPX5700AP (SEN0456) | `0x16`<BR>（Grove I2C, J5） | 参考監視のみ |
| 2次側空気圧センサ | DFRobot Gravity MPX5700AP (SEN0456) | `0x17`<BR>（Grove I2C, J6） | 制御量（最終判定に使用） |
| 燃圧センサ | アナログ 0.5–4.5V / 0–1.0MPa | ADS1015 `0x48` の AIN0 | 監視のみ（過圧判定・多重防御に使用） |
| ソレノイドバルブ | DC12V、ノーマリークローズ | `SOLENOID_PIN`<BR>（M5Core2 - GPIO19<BR>M5Stack Basic - GPIO13） | 自己適応パルス幅ON/OFF制御 |

上記3センサはすべて同一のI2Cバス（`Wire`）上に接続されており、SDA/SCLピンは `src/config.h` の `I2C_SDA_PIN` / `I2C_SCL_PIN` でボードごとに明示的に指定しています（ボードのデフォルトSDA/SCLには依存しません）。

1次側/2次側空気圧センサ（DFRobot_MPX5700）は、内部レジスタが元々0.01kPa単位（kPa×100の固定小数点）で値を保持しており、アプリ側でも丸めずfloatのままMPaへ変換してその分解能を維持しています。あわせて、センサ基板側の内蔵移動平均サンプル数（`MEAN_SAMPLE_SIZE_MPX5700`、`src/config.h`）でノイズを低減し、微小な圧力上昇をノイズに埋もれさせず検知できるようにしています。

| ボード | SDA | SCL | 備考 |
| --- | --- | --- | --- |
| M5Core2 | GPIO32 | GPIO33 | Port A（Grove）<BR>`M5.begin()` が使う内部I2Cバス（GPIO21/22、PMIC/RTC/タッチ/IMU用）とは独立しており競合しない |
| M5Stack Basic | GPIO2 | GPIO5 | 拡張基板（PCB）のM-BUS配線を本ピンに変更（旧: GPIO21/22＝Port A） |

## 制御ロジック

状態機械: `Init → Normal ⇄ PulseOpen → Cooldown → Normal`、および異常検知時は任意状態から `Fault` へ即遷移します。

- **Normal**: 2次側空気圧が目標帯下限（目標2次側空気圧−0.01MPa）未満なら現在のパルス幅だけバルブを開き `PulseOpen` へ。目標帯内、または上限（目標2次側空気圧＋0.01MPa）超過時はバルブ閉のまま待機（能動排気機構は無いため、上限超過時は自然消費を待ちます）。  
  目標2次側空気圧はボタン操作で実行時に変更可能（[目標2次側空気圧の調整（ボタン操作）](#目標2次側空気圧の調整ボタン操作)を参照）。
- **PulseOpen → Cooldown**: パルス終了後、`PULSE_COOLDOWN_MS` だけ休止してから再判定（配管内圧力の伝播待ち・センサ再安定待ち・コイルのデューティ抑制のため）。
- **自己適応パルス幅制御**: 配管容積とバルブ流量特性に対して1回のパルスが過大だと、1パルスで2次側空気圧が1次側とほぼ同じ圧力まで一気に上昇してしまう。これを防ぐため、パルス幅は最小値（`PULSE_WIDTH_MIN_MS`）から始め、Cooldown経過後の圧力判定結果に応じて次回のパルス幅を自動調整する（`Controller::adjustPulseWidth()`）。
  - 目標帯下限を下回ったまま（供給不足）→ 次回は伸長（`× PULSE_WIDTH_GROW_FACTOR`、上限`PULSE_WIDTH_MAX_MS`でクランプ）
  - 目標帯上限を超過（過供給）→ 次回は短縮（`× PULSE_WIDTH_SHRINK_FACTOR`、下限`PULSE_WIDTH_MIN_MS`でクランプ）
  - 目標帯内 → 現在の幅を維持（適正値として使い続ける）
  - フォルト発生時は安全側（最小幅）にリセットし、復帰後は再度学習し直す
- **Fault**: 以下を優先度順に判定し、検知したらバルブを強制的に閉じます（ヒステリシス・デバウンス付き）。
  1. センサ異常（I2C通信エラー・レンジ外の値・燃圧センサはADC読み取り値のばらつきによる未接続検出も含む。制御量ではない燃圧センサも多重防御として引き続き有効性を要求する）
  2. 過圧（燃圧 or 2次側が0.50MPa超過。制御量である2次側に加え、燃圧センサでも多重に検知する）
  3. 1次側供給不足（1次側が0.40MPa未満）
  4. 目標帯への復帰タイムアウト（5秒以上パルスを打ち続けても改善しない＝配管漏れ・弁固着・2次側空気圧センサの故障等の多重防御）

  センサ異常時は、警告表示およびシリアルログに `(P1 P2)` のように原因センサ（P1=1次側 / P2=2次側 / FUEL=燃圧）が付記され、どのセンサが異常かを個別に識別できます。

主要な制御定数（パルス幅・休止時間・しきい値など）はすべて [`src/config.h`](src/config.h) に集約されており、調整する場合はここを編集してください。

## 制御ループのタイミング

ミリ秒単位のパルス幅制御（最小 `PULSE_WIDTH_MIN_MS` = 5ms）を成立させるため、`main.cpp` の `loop()` は2つの異なる周期で構成されています。

- **バルブのオフ判定（`Controller::updateValve()` → `ValveDriver::update()`）**: 周期処理の間引きの外側で、毎ループ無条件に実行します。センサ読み取り周期（`SENSOR_READ_INTERVAL_MS` = 50ms）に巻き込んでしまうと、最小パルス幅より閉弁が最大50ms遅れて過供給を招くため、状態機械の更新（`Controller::update()`）とは意図的に呼び出し元を分離しています。
- **センサ読み取り・状態機械の更新（`Controller::update()`）**: `SENSOR_READ_INTERVAL_MS`（50ms）周期で実行します。

また、燃圧センサ（ADS1015）は起動時に連続変換モード（continuous conversion mode）へ切り替え、バックグラウンドで変換し続けます。`read()` 呼び出しごとに最新の変換結果を非ブロッキングで1個取得し、直近 `ADS1015_OVERSAMPLE_COUNT` 個をリングバッファで移動平均することでオーバーサンプルします（シングルショット変換の変換待ちによるメインループのブロッキング／ジッタを排除するため）。移動平均の実効窓幅は 約 `ADS1015_OVERSAMPLE_COUNT × SENSOR_READ_INTERVAL_MS`（既定値では約200ms）です。

## ファイル構成

```text
src/
├── config.h            # ピン番号・I2Cアドレス・全制御定数（BLE UUID含む）
├── system_types.h      # 状態機械の型定義（SystemState, FaultReason, SensorReadings等）
├── pressure_sensors.h/.cpp  # 3センサの初期化・読み取り・kPa/MPa変換・異常検知（燃圧センサはADS1015連続変換モードで非ブロッキング読み取り）
├── valve_driver.h/.cpp      # ソレノイドの非ブロッキング可変パルス駆動（毎ループ呼び出しでオフ判定）
├── controller.h/.cpp        # 状態機械・安全保護ロジック（バルブのオフ判定はupdateValve()として分離）
├── display_ui.h/.cpp        # M5Core2 LCDへのリアルタイム表示（BLE接続状態表示を含む）
├── ble_service.h/.cpp       # BLE PeripheralによるセンサデータNotify送信・接続状態管理
├── secondary_target_store.h/.cpp # 目標2次側空気圧の実行時保持・ボタン調整・NVS(Preferences)への永続化
└── main.cpp                 # setup()/loop()、各モジュールの統合とシリアルログ出力
```

## 目標2次側空気圧の調整（ボタン操作）

目標2次側空気圧（初期値0.35MPa）は、本体のA/B/Cボタンで実機上から調整・保存できます。

| ボタン | 操作 | 動作 |
| --- | --- | --- |
| A | 短押し | 目標2次側空気圧を `-0.01MPa` |
| C | 短押し | 目標2次側空気圧を `+0.01MPa` |
| B | 長押し（0.5秒以上） | 現在の目標2次側空気圧をNVS（ESP32 Preferences、EEPROM相当の不揮発領域）へ保存 |

- A/Cボタンでの調整は即座に制御（バルブ駆動判定）とLCD表示に反映されますが、**Bボタンで長押し保存するまでは電源を切ると失われ**、次回起動時は最後に保存した値（未保存なら初期値0.35MPa）に戻ります。
- 調整可能範囲は `0.10〜0.45MPa`（`SECONDARY_TARGET_MIN_MPA`〜`SECONDARY_TARGET_MAX_MPA`）に制限されており、これを超えて調整することはできません。過圧しきい値（`OVERPRESSURE_TRIP_MPA` = 0.50MPa）に対して安全マージンを確保しています。
- LCD画面のP2ゲージ行に現在の目標2次側空気圧が `TGT:0.35` のように表示され、保存前は末尾に `*` が付いて未保存であることを示します（保存後は消えます）。
- 調整・保存操作はシリアルログにも `[SETTINGS] P2 target -> 0.36MPa` / `[SETTINGS] P2 target 0.36MPa SAVED to NVS` の形式で出力されます。
- 各定数は [`src/config.h`](src/config.h) に集約されているため、範囲やステップ幅を変更する場合はここを編集してください。
- **既存機体をアップデートする場合の注意**: 本変更でNVS保存キーを `fuelTgt` から `secTgt` に変更したため、以前のファームウェアで保存していた目標値は引き継がれず、初回起動時は初期値（0.35MPa）から始まります。必要に応じて再度ボタン操作で目標値を調整・保存してください。

## BLE通信（M5NanoC6への中継）

本体はBLE Peripheral（Server）として動作し、[M5NanoC6_BLE_Central](https://github.com/todateman/M5NanoC6_BLE_Central)（BLE Central）に対して1次側/2次側空気圧・燃圧をNotifyで送信します。  
M5NanoC6はさらにI2C経由でM5Stack Basicへ中継します。

- デバイス名: `ChibiT-AutoAirAdjust`
- 送信周期: `DISPLAY_UPDATE_INTERVAL_MS`（100ms）ごと。BLEクライアントが接続している場合のみ送信
- 送信形式: `PRI:<P1のMPa値>\nSEC:<P2のMPa値>\nFUEL:<燃圧のMPa値>\n` を1回のNotifyでまとめて送信  
  （例: `PRI:0.85\nSEC:0.72\nFUEL:2.10\n`）
- センサ異常時（`SensorSample.valid == false`）は、直近の有効値を送り続けます  
  （安全制御は本体側の `Controller` が独立して担保するため、BLE Notifyは監視データの中継に徹します）
- 接続状態はLCD画面右上に `BLE:ON`（緑）/ `BLE:NO`（赤）として常時表示されます（`BleService::isConnected()`）

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
3. 定常運転中は状態遷移（`[STATE] ...`）、バルブの開閉（`[VALVE] OPEN/CLOSE t=... width=...ms`）、異常発生・解消（`[FAULT] ...`）がエッジトリガでログ出力される。  
   `width=` は自己適応中の現在のパルス幅で、供給不足が続くと伸長、過供給が起きると短縮されることを確認する。
4. LCD画面には1次側（P1）・2次側（P2）・燃圧（Fuel）がそれぞれ大きな数値とゲージバーでリアルタイム表示され、下部にバルブ状態、フォルト発生時のみ警告表示（赤背景）が表示される。  
   各ゲージの表示色はそのセンサ自身の有効性・しきい値（P1: `PRIMARY_SUPPLY_LOW_TRIP_MPA`、P2: 目標帯（制御量）、Fuel: `OVERPRESSURE_TRIP_MPA`、監視のみ）との比較のみで決まり、他センサの異常やFault遷移による影響は受けない。  
   画面右上には `BLE:ON`（緑）/ `BLE:NO`（赤）でBLEクライアントの接続状態が常時表示され、PCを繋がずシリアルログを見られない実機運用時でも接続の有無を一目で確認できる。
5. BLE通信は、起動時にシリアルログで `[BLE] advertising started` を確認。  
   M5NanoC6等のBLE Centralが接続すると `[BLE] client connected`、切断すると `[BLE] client disconnected, restarting advertising` が出力され、これと連動してLCD右上の表示も `BLE:NO`（赤）→ `BLE:ON`（緑）に切り替わることを確認する。  
   nRF Connect等のBLEスキャナアプリでも、デバイス名 `ChibiT-AutoAirAdjust` へ接続し、Notify Characteristic（`1d25ec49-...`）を購読することで送信データを直接確認できる。
6. デバッグ用に、BLE接続の有無によらず `DISPLAY_UPDATE_INTERVAL_MS`（100ms）ごとに `[BLE TX] PRI=... SEC=... FUEL=... (connected=yes/no)` がUSB Serialへ出力される（MPa値は5桁表示＝0.01kPa相当の分解能まで確認可能）。  
   BLE未接続でも送信予定データと接続状態をシリアルモニタだけで確認できる。  
   なお実際のBLE Notify送信ペイロード（`PRI:0.85\nSEC:0.72\n...`）は対向機実装に合わせてMPa2桁のまま。
7. Aボタン/Cボタンを押すたびにLCDのP2ゲージ行の `TGT:` 表示が0.01MPaずつ増減し、末尾に未保存を示す `*` が付くこと、`0.10〜0.45MPa` の範囲で頭打ちになることを確認する。  
   Bボタンを長押しするとシリアルログに `[SETTINGS] ... SAVED to NVS` が出力され、`*` が消えることを確認する。保存後に電源を入れ直し、調整した目標2次側空気圧が復元されることを確認する。

## 今後実施すべき実機検証

ソフトウェア側の制御パラメータ（パルス幅・休止時間・移動平均サンプル数）は、いずれもハードウェアの物理的な応答特性に合わせてチューニングする必要がありますが、以下2点は未計測のため暫定値のまま運用しています。  
オシロスコープ（PC接続型）を用いた実機計測が完了次第、`src/config.h` の該当定数を計測値に基づいて更新してください。

なお、Claude Code上の `VISA-MCP` 経由でRIGOL DHO804をSCPIコマンド操作するための接続情報（USBTMCリソースID・動作確認済みコマンド一覧・既知の問題）は [docs/oscilloscope_dho804_visa_mcp.md](docs/oscilloscope_dho804_visa_mcp.md) を参照してください。

1. **ソレノイドバルブのデッドタイム計測**  
   ソフトウェアがピン（`SOLENOID_PIN`）をHIGHにしてから、実際にバルブのプランジャーが動きエアーが流れ始めるまでの機械的な応答遅延（デッドタイム）を計測する。  
   - **計測手法**: チャンネル1でESP32のGPIO駆動信号（`SOLENOID_PIN`）の立ち上がりエッジをトリガーし、チャンネル2でバルブ通過直後の空気圧センサのアナログ実波形（I2Cでデジタル化される前の生電圧）を観測する。
   - **フィードバック**: 計測したデッドタイムを下回るパルス幅ではバルブが開かないため、計測値＋α（微小にエアーが流れる時間）を `PULSE_WIDTH_MIN_MS`（現在は暫定値5ms）に反映する。

2. **圧力伝播遅延と整定時間の計測**  
   バルブが閉じてから、配管内の圧力が均等化されセンサ値として安定するまでの時間を計測する。  
   - **計測手法**: 上記と同様のオシロスコープ設定で、GPIO駆動信号の立ち下がりエッジ（閉弁）から、センサのアナログ電圧波形の変動が完全に収束してフラットになるまでの時間を測る。
   - **フィードバック**: この整定時間が現在の `PULSE_COOLDOWN_MS`（300ms）より長い場合、ハンチング（自己適応パルス幅ロジックの過剰反応）の原因となるため、計測値に合わせて `PULSE_COOLDOWN_MS` を最適化する。必要に応じて2次側/1次側空気圧センサ（MPX5700）の移動平均サンプル数（`MEAN_SAMPLE_SIZE_MPX5700` = 30）もあわせてチューニングする。

   なお、燃圧センサ（ADS1015）は連続変換モードへの移行に伴い移動平均の実効窓幅が約200msへ広がっているため（[制御ループのタイミング](#制御ループのタイミング)参照）、フローティング（センサ未接続）検出の許容ばらつき `FUEL_ADC_MAX_SAMPLE_SPREAD_COUNTS` も実機波形を見ながら合わせて見直すことが望ましい。

## ライセンス

MIT License（詳細は [`LICENSE`](LICENSE) を参照）
