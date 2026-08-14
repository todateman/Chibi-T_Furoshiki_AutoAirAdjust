#include "ota_service.h"

#include <M5Unified.h>
#include <Update.h>
#include <WiFi.h>

#include "config.h"

namespace {

// アップロードフォームHTML。外部ライブラリ不使用の静的HTML+インラインJSのみ。
// ファイル選択時にブラウザ側でファイルサイズを読み取り、フォームのaction先を
// "/update?size=<bytes>" に書き換えてから送信する。書き込み自体はUPDATE_SIZE_UNKNOWNの
// ままで完結するため、このsizeヒントはLCD進捗表示専用(誤っていても書き込みの完全性には影響しない)。
const char kIndexHtml[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<title>ChibiT OTA Update</title></head><body>"
    "<h2>ChibiT_Furoshiki_AutoAirAdjust - OTA Update</h2>"
    "<form method=\"POST\" action=\"/update\" enctype=\"multipart/form-data\""
    " onsubmit=\"var f=document.getElementById('bin').files[0];"
    "if(!f){alert('\xe3\x83\x95\xe3\x82\xa1\xe3\x82\xa4\xe3\x83\xab\xe3\x82\x92\xe9\x81\xb8\xe6\x8a\x9e\xe3\x81\x97\xe3\x81\xa6\xe3\x81\x8f\xe3\x81\xa0\xe3\x81\x95\xe3\x81\x84');return false;}"
    "this.action='/update?size='+f.size;return true;\">"
    "<input id=\"bin\" type=\"file\" name=\"firmware\" accept=\".bin\"><br><br>"
    "<input type=\"submit\" value=\"Upload\">"
    "</form>"
    "<p>Do NOT power off during update.</p>"
    "</body></html>";

}  // namespace

void OtaService::failSafeCloseValve() {
  pinMode(SOLENOID_PIN, OUTPUT);
  digitalWrite(SOLENOID_PIN, LOW);  // フェイルセーフ: OTAモード中は必ず閉
}

void OtaService::beginSoftAp() {
  WiFi.mode(WIFI_AP);
  WiFi.setSleep(false);  // AP応答性確保(モデムスリープによるHTTPレイテンシ悪化を防ぐ)
  bool ok = WiFi.softAP(OTA_AP_SSID, OTA_AP_PASSWORD);
  Serial.printf("[OTA] softAP(ssid=%s) -> %s, IP=%s\n", OTA_AP_SSID, ok ? "OK" : "FAILED",
                WiFi.softAPIP().toString().c_str());
}

void OtaService::setupRoutes() {
  server_.on("/", HTTP_GET, [this]() { handleRoot(); });
  server_.on("/update", HTTP_POST, [this]() { handleUpdatePost(); },
              [this]() { handleUpdateUpload(); });
  server_.onNotFound([this]() {
    server_.sendHeader("Location", "/");
    server_.send(302);
  });
}

void OtaService::handleRoot() {
  server_.send(200, "text/html", kIndexHtml);
}

void OtaService::handleUpdateUpload() {
  HTTPUpload& upload = server_.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START: {
      declaredTotalBytes_ = server_.hasArg("size") ? server_.arg("size").toInt() : 0;
      updateHasError_ = false;
      lastDrawnPercent_ = 255;
      Serial.printf("[OTA] upload start: %s (size hint=%u)\n", upload.filename.c_str(),
                    static_cast<unsigned>(declaredTotalBytes_));
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        updateHasError_ = true;
        Update.printError(Serial);
      }
      drawProgress(0);
      break;
    }
    case UPLOAD_FILE_WRITE: {
      // begin()失敗後もEND到達まで受信だけ継続させ、最終応答(handleUpdatePost)で失敗を通知する
      if (updateHasError_) break;
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        updateHasError_ = true;
        Update.printError(Serial);
        break;
      }
      if (declaredTotalBytes_ > 0) {
        uint8_t pct = static_cast<uint8_t>(
            min<uint32_t>(99, (100ULL * upload.totalSize) / declaredTotalBytes_));
        drawProgress(pct);
      }
      break;
    }
    case UPLOAD_FILE_END: {
      // UPDATE_SIZE_UNKNOWN使用時は_sizeがパーティション全体サイズになるため、
      // evenIfRemaining=trueを指定しないと必ず「書き込みバイト数不足」判定で失敗する
      bool ok = !updateHasError_ && Update.end(true);
      if (!ok) updateHasError_ = true;
      Serial.printf("[OTA] upload end: %u bytes -> %s\n", static_cast<unsigned>(upload.totalSize),
                    ok ? "OK" : Update.errorString());
      drawProgress(100);
      break;
    }
    case UPLOAD_FILE_ABORTED: {
      Update.end(false);  // 内部状態をクリアし次回アップロードに備える
      updateHasError_ = true;
      Serial.println("[OTA] upload aborted by client");
      break;
    }
    default:
      break;
  }
}

void OtaService::handleUpdatePost() {
  bool success = !updateHasError_ && !Update.hasError();
  server_.sendHeader("Connection", "close");
  server_.send(200, "text/plain", success ? "OK" : "FAIL");

  if (success) {
    drawResult(true, "Success. Rebooting...");
    Serial.println("[OTA] update success, rebooting...");
    delay(1000);     // ブラウザへレスポンスを確実に送達させてから再起動する
    ESP.restart();   // 戻らない
  } else {
    const char* reason = Update.errorString();
    drawResult(false, reason);
    Serial.printf("[OTA] update FAILED: %s\n", reason);
    // AP/サーバーは落とさず待機状態に戻す(再アップロード可能。自動再起動しない)
  }
}

void OtaService::drawIdleScreen() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, 8);
  M5.Display.println("OTA UPDATE MODE");
  M5.Display.setTextSize(1);
  M5.Display.setCursor(8, 40);
  M5.Display.printf("1. Connect Wi-Fi\n   SSID: %s\n   PASS: %s\n\n", OTA_AP_SSID, OTA_AP_PASSWORD);
  M5.Display.printf("2. Open browser\n   http://%s/\n\n", WiFi.softAPIP().toString().c_str());
  M5.Display.println("3. Select .bin and Upload");
  M5.Display.setTextColor(TFT_YELLOW, TFT_BLACK);
  M5.Display.println("\nDo NOT power off during update.");
}

void OtaService::drawProgress(uint8_t percent) {
  // ちらつき防止のため、前回描画分から変化が無ければ再描画しない
  if (percent != 100 && percent == lastDrawnPercent_) return;
  lastDrawnPercent_ = percent;

  const int barX = 8, barY = 160, barW = 304, barH = 20;
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.fillRect(0, 140, 320, 40, TFT_BLACK);
  M5.Display.setCursor(barX, 140);
  M5.Display.printf("Uploading... %3u%%", percent);

  M5.Display.drawRect(barX, barY, barW, barH, TFT_WHITE);
  int fillW = (barW - 2) * percent / 100;
  M5.Display.fillRect(barX + 1, barY + 1, max(0, fillW), barH - 2, TFT_CYAN);
}

void OtaService::drawResult(bool success, const String& message) {
  uint16_t bg = success ? TFT_GREEN : TFT_RED;
  M5.Display.fillRect(0, 140, 320, 60, bg);
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_BLACK, bg);
  M5.Display.setCursor(8, 148);
  M5.Display.println(success ? "SUCCESS" : "FAILED");
  M5.Display.setTextSize(1);
  M5.Display.setCursor(8, 172);
  M5.Display.println(message);
  if (!success) {
    M5.Display.println("Retry from browser or power cycle.");
  }
}

void OtaService::run() {
  failSafeCloseValve();

  beginSoftAp();
  drawIdleScreen();
  setupRoutes();
  server_.begin();
  Serial.println("[OTA] HTTP server started, waiting for firmware upload...");

  for (;;) {
    server_.handleClient();
    digitalWrite(SOLENOID_PIN, LOW);  // フェイルセーフ多重防御(毎ループ書き戻す)
    delay(2);
  }
}
