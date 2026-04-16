#include <Wire.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include "u8g2_font_chinese_100.h" 
#include <WiFiClientSecure.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
#include "AudioTools/AudioCodecs/CodecAACHelix.h" // 確保有包含 AAC Helix
#include "AudioTools/Communication/HTTP/URLStream.h"
#include "AudioTools/AudioCodecs/CodecFactory.h" // 包含所有支援的解碼器工廠

const char* ssid     = "your wifi ssid";
const char* password = "wifi password";
const byte ROW[5] = {0, 15, 31, 47, 63};

#define I2S_BCK  5
#define I2S_LRC  6
#define I2S_DOUT 7

#define I2C_SDA 8
#define I2C_SCL 9
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA);

URLStream urlStream(ssid, password);
I2SStream i2s;
VolumeStream volume(i2s);
//MP3DecoderHelix decoder;
//EncodedAudioStream decStream(&volume, &decoder);

MP3DecoderHelix mp3;
AACDecoderHelix aac; // 或者 CodecAACFAAD
AudioDecoder *currentDecoder = &mp3; // 預設 MP3
EncodedAudioStream decStream(&volume, currentDecoder);

StreamCopy copier(decStream, urlStream);
WebServer server(80);

struct Radio {
  int id;
  String name;
  String url;
};

struct RadioData {
  Radio radios[10];
  int count;
  int current;
};

RadioData radioData;
bool isPlaying = false;
unsigned long lastDisplayUpdate = 0;
String currentStationName = "None";
String streamStatus = "Stopped";

void initFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return;
  }
  
  File file = SPIFFS.open("/radiourl.json", FILE_READ);
  if (!file) {
    Serial.println("Creating default radiourl.json...");
    DynamicJsonDocument doc(1024);
    JsonArray radios = doc.createNestedArray("radios");
    
    JsonObject r0 = radios.add<JsonObject>();
    r0["id"] = 0;
    r0["name"] = "Classic FM";
    r0["url"] = "http://media-ice.musicradio.com/ClassicFMMP3";
    
    JsonObject r1 = radios.add<JsonObject>();
    r1["id"] = 1;
    r1["name"] = "中廣音樂台";
    r1["url"] = "http://n15.rcs.revma.com/ndk05tyy2tzuv?rj-ttl=5&rj-tok=AAABnZVA7TgAyAcmpSzIdoCVZA";
    
    JsonObject r2 = radios.add<JsonObject>();
    r2["id"] = 2;
    r2["name"] = "ICE2";
    r2["url"] = "http://ice2.somafm.com/u80s-128-aac";
    
    doc["current"] = 0;
    
    File outFile = SPIFFS.open("/radiourl.json", FILE_WRITE);
    serializeJson(doc, outFile);
    outFile.close();
  }
  file.close();
}

void loadRadios() {
  File file = SPIFFS.open("/radiourl.json", FILE_READ);
  if (!file) return;
  
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) return;
  
  JsonArray radios = doc["radios"];
  radioData.count = radios.size();
  radioData.current = doc["current"] | 0;
  
  for (int i = 0; i < radioData.count && i < 10; i++) {
    radioData.radios[i].id = radios[i]["id"];
    radioData.radios[i].name = radios[i]["name"].as<String>();
    radioData.radios[i].url = radios[i]["url"].as<String>();
  }
}

void saveRadios() {
  DynamicJsonDocument doc(2048);
  JsonArray radios = doc.createNestedArray("radios");
  
  for (int i = 0; i < radioData.count; i++) {
    JsonObject r = radios.add<JsonObject>();
    r["id"] = radioData.radios[i].id;
    r["name"] = radioData.radios[i].name;
    r["url"] = radioData.radios[i].url;
  }
  
  doc["current"] = radioData.current;
  
  File file = SPIFFS.open("/radiourl.json", FILE_WRITE);
  serializeJson(doc, file);
  file.close();
}

void playRadio(int id) {
    isPlaying = false; 
    urlStream.end();
    delay(200);
//    urlStream.setMetadataCallback(nullptr);
    String targetUrl = radioData.radios[id].id == 0 ? radioData.radios[id].url : radioData.radios[id].url;
    currentStationName = radioData.radios[id].name;
    radioData.current = id;
    Serial.printf("Station : [%d]%s\n",id,currentStationName.c_str());
    // 1. 嘗試連線
    if (urlStream.begin(targetUrl.c_str(), "audio/mpeg")) {
        
        // 2. 取得 MIME Type (直接傳入 "Content-Type" 給 getReplyHeader)
        const char* contentType = urlStream.getReplyHeader("Content-Type");
        String mime = (contentType != NULL) ? String(contentType) : "";

        // 3. 動態判斷解碼器 (依據 MIME 或網址)
        if (mime.indexOf("aac") >= 0) {
            currentDecoder = &aac;
        } else {
            currentDecoder = &mp3;
        }

        // --- 核心修正：避免 I2S 鎖死 ---
//    i2s.end();
//    i2s.begin(i2s.defaultConfig(TX_MODE));
    
        i2s.end(); // 先徹底關閉 I2S
        auto config = i2s.defaultConfig(TX_MODE);
        config.pin_bck = I2S_BCK;
        config.pin_ws = I2S_LRC;
        config.pin_data = I2S_DOUT;
        i2s.begin(config); // 重新初始化，確保硬體狀態乾淨

        // 4. 更新解碼器並啟動
        decStream.end();
        decStream.setDecoder(currentDecoder);
        
        if (decStream.begin()) {
          copier.begin(decStream, urlStream);
          streamStatus = "Playing..";
          isPlaying = true;
          }       

    } else {
        Serial.println("Connection Failed");
        streamStatus = "Connect Error";
    }
    updateDisplay();
}


void stopRadio() {
  urlStream.end();
  isPlaying = false;
  streamStatus = "Stopped";
  updateDisplay();
}

void updateDisplay() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_unifont_myfonts);
  u8g2.setCursor(0, ROW[1]);
  u8g2.print(currentStationName.c_str());
//  u8g2.drawUTF8(0, 16, currentStationName.c_str());
  
//  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setCursor(0, ROW[2]);
  u8g2.print(streamStatus);
  
  char buf[32];
  snprintf(buf, sizeof(buf), "Station: %d/%d", radioData.current + 1, radioData.count);
  u8g2.setCursor(0, ROW[3]);
  u8g2.print(buf);
  
  u8g2.sendBuffer();
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>StreamRadio</title>";
  html += "<style>body{font-family:Arial;margin:20px;background:#1a1a1a;color:#fff}";
  html += ".card{background:#2a2a2a;padding:20px;margin:10px 0;border-radius:10px}";
  html += ".btn{display:inline-block;padding:10px 20px;margin:5px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:5px}";
  html += ".btn-stop{background:#f44336}.btn-edit{background:#2196F3}";
  html += ".station-list{list-style:none;padding:0}";
  html += ".station-item{background:#333;padding:10px;margin:5px 0;border-radius:5px;display:flex;justify-content:space-between}";
  html += "input,select{padding:8px;margin:5px;width:200px;background:#444;color:#fff;border:1px solid #666;border-radius:4px}";
  html += "</style></head><body>";
  html += "<h1>StreamRadio Web Control</h1>";
  html += "<div class='card'>";
  html += "<h2>Now Playing</h2>";
  html += "<p>Station: <strong>" + currentStationName + "</strong></p>";
  html += "<p>Status: <strong>" + streamStatus + "</strong></p>";
  html += "<a href='/play/0' class='btn'>Stop</a>";
  html += "<a href='/edit' class='btn btn-edit'>Manage Stations</a>";
  html += "</div>";
  html += "<div class='card'><h2>Stations</h2><ul class='station-list'>";
  Serial.println(radioData.current);
  for (int i = 0; i < radioData.count; i++) {
    String active = (i == radioData.current) ? " [Playing]" : "";
    html += "<li class='station-item'>";
    html += "<span>" + radioData.radios[i].name + active + "</span>";
    html += "<div><a href='/play?id=" + String(i) + "' class='btn'>Play</a></div>";
    html += "</li>";
  }
  
  html += "</ul></div></body></html>";
  
  server.send(200, "text/html; charset=utf-8", html);
}

void handleEdit() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Manage Stations</title>";
  html += "<style>body{font-family:Arial;margin:20px;background:#1a1a1a;color:#fff}";
  html += ".card{background:#2a2a2a;padding:20px;margin:10px 0;border-radius:10px}";
  html += ".btn{display:inline-block;padding:10px 20px;margin:5px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:5px}";
  html += ".btn-back{background:#666}.btn-del{background:#f44336}";
  html += "input{width:200px;padding:8px;margin:5px;background:#444;color:#fff;border:1px solid #666;border-radius:4px}";
  html += "label{display:inline-block;width:80px}";
  html += "</style></head><body>";
  html += "<h1>Manage Stations</h1>";
  html += "<a href='/' class='btn btn-back'>Back</a>";
  
  html += "<div class='card'><h2>Add New Station</h2>";
  html += "<form action='/add' method='POST'>";
  html += "<label>Name:</label><input type='text' name='name' required><br>";
  html += "<label>URL:</label><input type='text' name='url' required><br>";
  html += "<button type='submit' class='btn'>Add Station</button>";
  html += "</form></div>";
  
  html += "<div class='card'><h2>Edit Stations</h2>";
  for (int i = 0; i < radioData.count; i++) {
    html += "<form action='/update?id=" + String(i) + "' method='POST' style='margin:10px 0'>";
    html += "<input type='text' name='name' value='" + radioData.radios[i].name + "'>";
    html += "<input type='text' name='url' value='" + radioData.radios[i].url + "' style='width:300px'>";
    html += "<button type='submit' class='btn'>Update</button>";
    html += "<a href='/delete?id=" + String(i) + "' class='btn btn-del'>Delete</a>";
    html += "</form>";
  }
  html += "</div></body></html>";
  
  server.send(200, "text/html; charset=utf-8", html);
}


void handleStop() {
  stopRadio();
  server.sendHeader("Location", "/");
  server.send(302);
}

void handleAdd() {
  if (server.hasArg("name") && server.hasArg("url") && radioData.count < 10) {
    radioData.radios[radioData.count].id = radioData.count;
    radioData.radios[radioData.count].name = server.arg("name");
    radioData.radios[radioData.count].url = server.arg("url");
    radioData.count++;
    saveRadios();
  }
  server.sendHeader("Location", "/edit");
  server.send(302);
}

void handleUpdate() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    if (id >= 0 && id < radioData.count && server.hasArg("name") && server.hasArg("url")) {
      radioData.radios[id].name = server.arg("name");
      radioData.radios[id].url = server.arg("url");
      saveRadios();
    }
  }
  server.sendHeader("Location", "/edit");
  server.send(302);
}

void handleDelete() {
  if (server.hasArg("id")) {
    int id = server.arg("id").toInt();
    if (id >= 0 && id < radioData.count) {
      for (int i = id; i < radioData.count - 1; i++) {
        radioData.radios[i] = radioData.radios[i + 1];
        radioData.radios[i].id = i;
      }
      radioData.count--;
      if (radioData.current >= radioData.count) radioData.current = 0;
      saveRadios();
    }
  }
  server.sendHeader("Location", "/edit");
  server.send(302);
}

void handleNotFound() {
  server.send(404, "text/plain", "Not Found");
}


void setup() {
  delay(500);
  Serial.begin(115200);


  
  initFS();
  loadRadios();
  
  Wire.begin(I2C_SDA, I2C_SCL);
  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.clearBuffer();
//  u8g2.firstPage();
//  do {
    u8g2.setFont(u8g2_font_ncenB12_tr);
    u8g2.setCursor(0, ROW[1]);
    u8g2.print(currentStationName.c_str());
//    u8g2.print(currentStationName.substring(0, 15));
    
//    u8g2.setFont(u8g2_font_6x10_tr);
    u8g2.setCursor(0, ROW[2]);
    u8g2.print(streamStatus);
    
    u8g2.setCursor(0, ROW[3]);
    u8g2.printf("Station: %d/%d", radioData.current + 1, radioData.count);
//  } while (u8g2.nextPage());
    u8g2.sendBuffer();

//  AudioLogger::instance().begin(Serial, AudioLogger::Info);
  AudioLogger::instance().begin(Serial, AudioLogger::Warning);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.begin();
  // 等待 5 秒確認是否能自動連上
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 10) {
    vTaskDelay(pdMS_TO_TICKS(500));
    retry++;
    }
 
  auto config = i2s.defaultConfig(TX_MODE);
  config.pin_bck = I2S_BCK;
  config.pin_ws = I2S_LRC;
  config.pin_data = I2S_DOUT;
  i2s.begin(config);
  
  volume.begin(config);
  volume.setVolume(0.9);
  
  decStream.begin();
  copier.begin(decStream, urlStream);  
  server.on("/", HTTP_GET, handleRoot);
  server.on("/edit", HTTP_GET, handleEdit);
  server.on("/play/0", HTTP_GET, handleStop);
  // 播放路由 (使用 /play?id=X)
  server.on("/play", HTTP_GET, []() {
    if (server.hasArg("id")) {
      int id = server.arg("id").toInt();       
      playRadio(id);
    }
    server.sendHeader("Location", "/");
    server.send(302);
  });
  server.on("/add", HTTP_POST, handleAdd);
  server.on("/update", HTTP_POST, handleUpdate);
  server.on("/delete", HTTP_GET, handleDelete);
  server.onNotFound(handleNotFound);
  
  server.begin();
  
  Serial.println("Connecting to WiFi...");
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setCursor(10, 30);
  u8g2.print("Connecting WiFi...");
  u8g2.sendBuffer();
  
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
    currentStationName = radioData.radios[radioData.current].name;
    playRadio(radioData.current);
  } else {
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB12_tr);
    u8g2.setCursor(10, 30);
    u8g2.print("WiFi Failed");
    u8g2.sendBuffer();
  }
}

void loop() {

    // 優先處理音訊搬運
    if (isPlaying) {
        copier.copy();
    }

    // 每 5 次循環才處理一次 WebServer，減少對音訊的干擾
    static int webCount = 0;
    if (webCount++ > 5) {
        server.handleClient();
        webCount = 0;
    }
  
  if (millis() - lastDisplayUpdate > 1000) {
    lastDisplayUpdate = millis();
    if (WiFi.status() == WL_CONNECTED) {
      updateDisplay();
    }
  }
}
