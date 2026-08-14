#pragma once

#include <Arduino.h>
#include <WebServer.h>

// 起動時Bボタン押下でのみ突入する、Wi-Fi AP経由ブラウザアップロード方式のOTA更新モード。
// main.cpp::setup()の先頭付近から一度だけrun()を呼び出す。
//
// 通常運転系(PressureSensors/Controller/DisplayUI/BleService/SecondaryTargetStore)には
// 一切依存させず、このクラス単体で自己完結させる。理由:
//   - OTA中に不要な処理を極力削って安全側に倒す
//   - Wi-Fi + BLE同時運用(coexistence)の複雑さを最初から回避する
//   - I2Cバスに一切触れないため、センサ関連の異常挙動と無関係にできる
//
// run()はブロッキングで、内部で無限ループを回す。更新成功時はESP.restart()で再起動するため
// 戻らない。更新失敗時はAP/WebServerを維持したままループを継続し、電源を切らない限り
// 再アップロードを受け付け続ける(依頼どおりリトライ可能な設計)。
class OtaService {
 public:
  void run();  // ブロッキング。通常は戻らない([[noreturn]]ではないが実質そう振る舞う)

 private:
  WebServer server_{80};

  bool updateHasError_ = false;
  size_t declaredTotalBytes_ = 0;  // ブラウザ側JSが送ってくる?size=ヒント(進捗表示専用、書き込みには不使用)
  uint8_t lastDrawnPercent_ = 255; // LCD再描画の間引き用(初期値255="未描画"を表す番兵)

  void beginSoftAp();
  void setupRoutes();

  void handleRoot();          // GET  /       -> アップロードフォームHTML
  void handleUpdatePost();    // POST /update -> アップロード完了後の最終レスポンス+再起動判断
  void handleUpdateUpload();  // POST /update -> アップロードストリーム本体(Update.write())

  // フェイルセーフ: 通常運転用のValveDriver/Controllerは初期化しない(OTAモードは
  // 通常運転系と完全に独立させる設計方針のため)。ソレノイド駆動ピンを安全な閉状態にする
  // 最小限の処理のみ行う(valve_driver.cppのbegin()と同じ考え方を意図的に複製している。
  // esp_timer等のパルス駆動機構はOTAモードでは不要なため依存を持たせない)。
  void failSafeCloseValve();

  void drawIdleScreen();
  void drawProgress(uint8_t percent);
  void drawResult(bool success, const String& message);
};
