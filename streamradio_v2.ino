/*
 * 網路串流收音機(穩定完成版)
 * 2026.04.20 by Halin
 * 硬體架構:ESP32 c3 super mini + Max98357a + SSD1306 I2C 128*64
 * 使用WiFi登入網路後透過WebServer設定新增刪除控制音量等功能
 * 目前支援串流格式為mpeg/aac兩種
 * WIFI使用esptouch掃描設定
 * 2026.04.22
 *  version 2.0
 *  新增直接透過http://de1.api.radio-browser.info搜尋全球電台
 *  限制最大取樣率128k(c3不夠力了)
 * 
*/
//#define use_def_pwd //使用自定義SSID password
#define serialDSP
#include <Wire.h>
#include <WiFi.h>
#include <SPIFFS.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <U8g2lib.h>
#include "u8g2_font_chinese_100.h" 
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "AudioTools.h"
#include "AudioTools/AudioCodecs/CodecMP3Helix.h"
//#include "AudioTools/AudioCodecs/CodecMP3Mini.h"
#include "AudioTools/AudioCodecs/CodecAACHelix.h" // 確保有包含 AAC Helix
#include "AudioTools/Communication/HTTP/URLStream.h"
#include "AudioTools/AudioCodecs/CodecFactory.h" // 包含所有支援的解碼器工廠
#ifdef use_def_pwd
const char* ssid     = "D-Link_DIR-612";
const char* password = "27687425";
#endif

const char* RB_HOST = "http://de1.api.radio-browser.info";

struct CountryEntry {
  const char* code;
  const char* name;
};
static const CountryEntry COUNTRIES[] = {
  { "all", "All" },
  { "AE", "UAE" },
  { "AU", "Australia" },
  { "BR", "Brazil" },
  { "CA", "Canada" },
  { "CN", "China" },
  { "DE", "Germany" },
  { "ES", "Spain" },
  { "FR", "France" },
  { "GB", "UK" },
  { "HK", "HongKong" },
  { "IN", "India" },
  { "IT", "Italy" },
  { "JP", "Japan" },
  { "KR", "KOREA" },
  { "MX", "Mexico" },
  { "NL", "Netherlands" },
  { "NO", "Norway" },
  { "SE", "Sweden" },
  { "SG", "Singapore" },
  { "TW", "Taiwan" },
  { "US", "USA" },
  { "ZA", "South Africa" },
};
static const int COUNTRY_COUNT = sizeof(COUNTRIES) / sizeof(COUNTRIES[0]);

struct GenreEntry {
  const char* tag;
  const char* label;
};
static const GenreEntry GENRES[] = {
  { "all", "All" },
  { "music", "Music" },
  { "news", "News" },
  { "jazz", "Jazz" },
  { "classical", "Classical" },
  { "rock", "Rock" },
  { "pop", "Pop" },
  { "electronic", "Electronic" },
  { "talk", "Talk" },
};
static const int GENRE_COUNT = sizeof(GENRES) / sizeof(GENRES[0]);
const byte ROW[5] = {0, 15, 31, 47, 63};
//MAX98357a I2S接腳定義
#define I2S_BCK  5
#define I2S_LRC  6
#define I2S_DOUT 7
//SSD1306 I2C接腳定義
#define I2C_SDA 8
#define I2C_SCL 9
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, I2C_SCL, I2C_SDA);
#ifdef use_def_pwd
URLStream urlStream(ssid, password);
#else
URLStream urlStream;
#endif
I2SStream i2s;
VolumeStream volume(i2s);
//ChannelReducer reducer(volume); 
//MP3DecoderHelix decoder;
//EncodedAudioStream decStream(&volume, &decoder);

MP3DecoderHelix mp3;
//MP3DecoderMini mp3;
AACDecoderHelix aac; // 或者 CodecAACFAAD
AudioDecoder *currentDecoder = &mp3; // 預設 MP3
EncodedAudioStream decStream(&volume, currentDecoder);

StreamCopy copier(decStream, urlStream);
WebServer server(80);
const char* headerKeys[] = {"Accept-Language"};
const size_t headerKeysCount = sizeof(headerKeys) / sizeof(headerKeys[0]);

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
bool onReConnect =false;
static unsigned long lastDataMillis = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastEQUpdate = 0;
String currentStationName = "None";
String streamStatus = "Stopped";
float currentVolume = 0.9;
String tmpurl = "";

void initFS() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS mount failed");
    return;
  }
  
//  File file = SPIFFS.open("/radiourl.json", FILE_READ);
  if (!SPIFFS.exists("/radiourl.json")) {
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
//  file.close();
}

void listSPIFFSFiles() {
#ifdef serialDSP      
    Serial.println("目前 SPIFFS 檔案清單：");
#endif    
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    while (file) {
#ifdef serialDSP      
        Serial.printf("檔案: %s, 大小: %d bytes\n", file.name(), file.size());
#endif        
        file = root.openNextFile();
    }
}


void loadRadios() {
  File file = SPIFFS.open("/radiourl.json", FILE_READ);
  if (!file) return;
#ifdef serialDSP      
  Serial.println("load json file");
#endif  
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  
  if (error) {
    SPIFFS.remove("/radiourl.json");
    return;
    }
  JsonArray radios = doc["radios"];
  radioData.count = radios.size();
  radioData.current = doc["current"] | 0;
  
  for (int i = 0; i < radioData.count && i < 10; i++) {
    radioData.radios[i].id = radios[i]["id"];
    radioData.radios[i].name = radios[i]["name"].as<String>();
    radioData.radios[i].url = radios[i]["url"].as<String>();
  }
#ifdef serialDSP      
  Serial.printf("[0]url :%s\n",radioData.radios[0].url.c_str());
#endif  
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
    if(isPlaying){
    urlStream.end();
    decStream.end();
    i2s.end(); // 徹底關閉 I2S
    }
    isPlaying = false; 
    delay(200);

    if(onReConnect){return;}
    onReConnect = true;    
    Serial.printf("連線前Heap: Free %d(Max %d)\n", ESP.getFreeHeap(),ESP.getMaxAllocHeap());

//    urlStream.setMetadataCallback(nullptr);
    String targetUrl = radioData.radios[id].id == 0 ? radioData.radios[id].url : radioData.radios[id].url;
    if(tmpurl.length() > 0){
      targetUrl =tmpurl;
      tmpurl="";
      } else {
    currentStationName = radioData.radios[id].name;
      }
    radioData.current = id;
    Serial.printf("Station : [%d]%s\n",id,currentStationName.c_str());
//    urlStream.httpRequest().addRequestHeader("Connection", "keep-alive");
//    urlStream.httpRequest().addRequestHeader("User-Agent", "VLC/3.0.12 LibVLC/3.0.12");
    // 1. 嘗試連線
    if (urlStream.begin(targetUrl.c_str())) {
//    if (urlStream.begin(targetUrl.c_str(), "audio/mpeg")) {
        
    // 2. 取得 MIME Type (直接傳入 "Content-Type" 給 getReplyHeader)
        const char* contentType = urlStream.getReplyHeader("Content-Type");
        String mime = (contentType != NULL) ? String(contentType) : "";

        // 3. 動態判斷解碼器 (依據 MIME 或網址)
        if (mime.indexOf("aac") >= 0) {
            currentDecoder = &aac;
        } else {
            currentDecoder = &mp3;
        }
    
//        i2s.end(); // 先徹底關閉 I2S
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
          lastDataMillis = millis();
          isPlaying = true;
          }       

    } else {
        streamStatus = "Connect Error";
        Serial.println(streamStatus);
    }
    updateDisplay();
    onReConnect = false;
}


void stopRadio() {
  urlStream.end();
  decStream.end();
  i2s.end(); // 徹底關閉 I2S
  isPlaying = false;
  streamStatus = "Stopped";
  Serial.println("StreamStopped");
  updateDisplay();
}

void setVolume(float vol) {
  currentVolume = constrain(vol, 0.0, 1.0);
  volume.setVolume(currentVolume);
  Serial.printf("Volume set to: %.1f\n", currentVolume);
}

void handleVolume() {
  if (server.hasArg("vol")) {
    float vol = server.arg("vol").toFloat();
    setVolume(vol);
  }
  server.send(200, "text/plain", String(currentVolume));
}

void drawEQBars() {
  unsigned long currentTime = millis();
  int barWidth = 12;
  int barSpacing = 4;
  int maxHeight = 34;
  int minHeight = 4;
  int startX = 0;
  int barY = 46;
  
  for (int i = 0; i < 8; i++) {
    int height;
    if (isPlaying) {
      float phase = currentTime / 150.0 + i * 0.8;
      float noise = random(-30, 30) / 100.0;
      height = minHeight + (maxHeight - minHeight) * (0.4 + 0.3 * sin(phase) + 0.3 * noise + 0.2 * sin(phase * 2.3));
    } else {
      height = minHeight;
    }
    height = constrain(height, minHeight, maxHeight);
    
    int x = startX + i * (barWidth + barSpacing);
    u8g2.drawBox(x, barY - height, barWidth - 1, height);
  }
}

void updateDisplay() {
  u8g2.clearBuffer();
  
  u8g2.setFont(u8g2_font_unifont_myfonts);
  u8g2.setCursor(0, 15);
  u8g2.print(currentStationName.c_str());

  if (isPlaying) {
    drawEQBars();
    } else {
    u8g2.setCursor(0, 47);
    u8g2.print(streamStatus);      
    }
  
  u8g2.setCursor(0, 63);
  u8g2.print(WiFi.localIP().toString().c_str());
  
  u8g2.sendBuffer();
}

void handleRoot() {
 String lang =""; 
 bool isChinese = false;
  if (server.hasHeader("Accept-Language")) {
  lang = server.header("Accept-Language");
  isChinese = (lang.indexOf("zh") != -1); 
  }
  
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>";
  html += isChinese ? "網路音樂播放站" : "StreamRadio";
  html += "</title><style>body{font-family:Arial;margin:20px;background:#1a1a1a;color:#fff}";
  html += ".card{background:#2a2a2a;padding:20px;margin:10px 0;border-radius:10px}";
  html += ".btn{display:inline-block;padding:10px 20px;margin:5px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:5px}";
  html += ".btn-stop{background:#f44336}.btn-edit{background:#2196F3}";
  html += ".station-list{list-style:none;padding:0}";
  html += ".station-item{background:#333;padding:10px;margin:5px 0;border-radius:5px;display:flex;justify-content:space-between}";
  html += "input,select{padding:8px;margin:5px;width:200px;background:#444;color:#fff;border:1px solid #666;border-radius:4px}";
  html += "#volume-slider{width:100%;height:10px;margin:10px 0}";
  html += ".vol-control{margin:10px 0}";
  html += "</style></head><body><h1>";
  html += isChinese ? "網路音樂播放站" : "StreamRadio Web Control";  
//html += "StreamRadio Web Control";
  html += "</h1><div class='card'>";
//  html += "<h2>Now Playing</h2><p>";
  html += isChinese ? "目前播放位址:" : "Station:";  
//  html += "Station:";
  html += " <strong>" + currentStationName + "</strong></p><p>";
  html += isChinese ? "狀態" : "Status:";  
//  html += "Status:";
  html += " <strong>" + streamStatus + "</strong></p>";
  html += "<a href='/play/0' class='btn'>";
  html += isChinese ? "停止" : "Stop";  
//  html += "Stop";
  html += "</a><a href='/edit' class='btn btn-edit'>";
  html += isChinese ? "設定" : "Manage";  
  html += "</a><a href='/search' class='btn' style='background:#9C27B0'>";
  html += isChinese ? "搜尋電台" : "Search";  
  html += "</a><div class='vol-control'>";
  html += isChinese ? "音量" : "Volume: ";  
//  html += "Volume: ";
  html += "<input type='range' id='volume-slider' min='0' max='10' step='1' value='" + String(int(currentVolume * 10)) + "'>";
  html += "</div>";
  html += "</div>";
  html += "<div class='card'><h2>";
  html += isChinese ? "現有播放來源" : "Stations";  
//  html += "Stations";
  html += "</h2><ul class='station-list'>";
//  Serial.println(radioData.current);
  for (int i = 0; i < radioData.count; i++) {
    String active = (i == radioData.current) ? " [Playing]" : "";
    html += "<li class='station-item'>";
    html += "<span>" + radioData.radios[i].name + active + "</span>";
    html += "<div><a href='/play?id=" + String(i) + "' class='btn'>Play</a></div>";
    html += "</li>";
  }
  
  html += "</ul></div>";
  html += "<script>document.getElementById('volume-slider').addEventListener('input',function(){var xhr=new XMLHttpRequest();xhr.open('POST','/volume',true);xhr.setRequestHeader('Content-Type','application/x-www-form-urlencoded');xhr.send('vol='+(this.value/10));});</script>";
  html += "</body></html>";
  
  server.send(200, "text/html; charset=utf-8", html);
}

void handleEdit() {
 String lang =""; 
 bool isChinese = false;
  if (server.hasHeader("Accept-Language")) {
  lang = server.header("Accept-Language");
  isChinese = (lang.indexOf("zh") != -1); 
  }
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Manage Stations</title>";
  html += "<style>body{font-family:Arial;margin:20px;background:#1a1a1a;color:#fff}";
  html += ".card{background:#2a2a2a;padding:20px;margin:10px 0;border-radius:10px}";
  html += ".btn{display:inline-block;padding:10px 20px;margin:5px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:5px}";
  html += ".btn-back{background:#666}.btn-del{background:#f44336}";
  html += "input{width:200px;padding:8px;margin:5px;background:#444;color:#fff;border:1px solid #666;border-radius:4px}";
  html += "label{display:inline-block;width:80px}";
  html += "#volume-slider{width:100%;height:10px;margin:10px 0}";
  html += ".vol-control{margin:10px 0}";
  html += "</style></head><body><h1>";
  html += isChinese ? "來源位址管理" : "Manage Stations";  
//  html += "Manage Stations";
  html += "</h1><a href='/' class='btn btn-back'>";
  html += isChinese ? "回首頁" : "Back";  
//  html += "Back";  
  
  html += "</a><div class='card'><h2>";
  html += isChinese ? "新增來源位址" : "Add New Station";  
//  html += "Add New Station";
  html += "</h2><form action='/add' method='POST'><label>";
  html += isChinese ? "自訂名稱" : "Name";  
//  html += "Name";
  html += ":</label><input type='text' name='name' required><br><label>";
  html += isChinese ? "真實URL" : "URL";  
//  html += "URL";
  html += ":</label><input type='text' name='url' required><br>";
  html += "<button type='submit' class='btn'>";
  html += isChinese ? "確定" : "Add Station";  
//  html += "Add Station";
  html += "</button></form></div>";
  
  html += "<div class='card'><h2>Edit Stations</h2>";
  for (int i = 0; i < radioData.count; i++) {
    html += "<form action='/update?id=" + String(i) + "' method='POST' style='margin:10px 0'>";
    html += "<input type='text' name='name' value='" + radioData.radios[i].name + "'>";
    html += "<input type='text' name='url' value='" + radioData.radios[i].url + "' style='width:300px'>";
    html += "<button type='submit' class='btn'>Update</button>";
    html += "<a href='/delete?id=" + String(i) + "' class='btn btn-del'>Delete</a>";
    html += "</form>";
  }
  html += "</div>";
  html += "<script>document.getElementById('volume-slider').addEventListener('input',function(){var xhr=new XMLHttpRequest();xhr.open('POST','/volume',true);xhr.setRequestHeader('Content-Type','application/x-www-form-urlencoded');xhr.send('vol='+(this.value/10));});</script>";
  html += "</body></html>";
  
  server.send(200, "text/html; charset=utf-8", html);
}
/*
void handlePlay() {
  int id = server.pathArg(0).toInt();
  Serial.printf("id=%d\n",id);
  playRadio(id);
  server.sendHeader("Location", "/");
  server.send(302);
}
*/
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

void handleApiStations() {
  String country = server.hasArg("country") ? server.arg("country") : "all";
  String genre = server.hasArg("genre") ? server.arg("genre") : "all";
  int page = server.hasArg("page") ? server.arg("page").toInt() : 0;
  int limit = 20;

  HTTPClient http;
  String url = String(RB_HOST) + "/json/stations/search?limit=" + String(limit * 2) + "&offset=" + String(page * limit) + "&hidebroken=true&order=clickcount&reverse=true";
 Serial.printf("page:%d country:%s genre:%s\n",page,country.c_str(),genre.c_str()); 
  
  if (genre != "all") url += "&tag=" + genre;
  if (country != "all") url += "&countrycode=" + country;
//  url += "&bitrateMin=64";
 Serial.println(url.c_str()); 

  http.begin(url);
  http.addHeader("User-Agent", "ESP32Radio/1.0");
  http.setTimeout(8000); // 超時保護
  http.useHTTP10(true); // 在 http.begin() 之後、http.GET() 之前加入此行
  int code = http.GET();
  
  if (code != 200) {
    http.end();
    server.send(500, "application/json", "{\"error\":\"API request failed\"}");
    Serial.println("API request failed"); 
    return;
  }

// Filter
StaticJsonDocument<256> filter;
filter[0]["name"] = true;
filter[0]["url"] = true;          
filter[0]["url_resolved"] = true;
filter[0]["codec"] = true;
filter[0]["bitrate"] = true;
filter[0]["countrycode"] = true;

// JSON Doc
DynamicJsonDocument doc(8192);
DeserializationError error = deserializeJson(doc, http.getStream(),  DeserializationOption::Filter(filter));

  if (error) {
    server.send(500, "application/json", "{\"error\":\"Parse JSON failed\"}");
    Serial.println("Parse JSON failed"); 
    return;
  }

// 4. 檢查 Array 是否為空
JsonArray arr = doc.as<JsonArray>();
Serial.printf("Array size: %d\n", arr.size());


  String result = "[";
  int count = 0;
  for (JsonObject s : arr) {
//    String u = s["url"] | "";
    String u = s["url_resolved"] | "";
    if (!u.startsWith("http://") && !u.startsWith("https://")) continue;
    
    String codec = s["codec"].as<String>();
    codec.toUpperCase();
    if (codec != "AAC" && codec != "MP3") continue;
    if (codec == "MP3" && s["bitrate"].as<int>() > 192) continue;
    
    
    if (count > 0) result += ",";
    result += "{\"name\":\"" + s["name"].as<String>() + "\",";
    result += "\"url\":\"" + u + "\",";
    result += "\"country\":\"" + s["countrycode"].as<String>() + "\",";
    result += "\"codec\":\"" + s["codec"].as<String>() + "\",";
    result += "\"bitrate\":" + String(s["bitrate"].as<int>()) + "}";
    count++;

    if (count >= limit) break;
  }
  
  http.end();
  result += "]";
  Serial.printf("item: %s\n",result.c_str()); 
  
  server.send(200, "application/json", result);
}

void handleApiAddStation() {
  if (server.hasArg("name") && server.hasArg("url") && radioData.count < 10) {
    radioData.radios[radioData.count].id = radioData.count;
    radioData.radios[radioData.count].name = server.arg("name");
    radioData.radios[radioData.count].url = server.arg("url");
    radioData.count++;
    saveRadios();
    server.send(200, "application/json", "{\"success\":true}");
  } else {
    server.send(400, "application/json", "{\"error\":\"Invalid request or max stations reached\"}");
  }
}

void handleApiTempPlay() {
  if (server.hasArg("name") && server.hasArg("url")) {
    String name = server.arg("name");
    String url = server.arg("url");
    Serial.printf("play name:%s url:%s\n",name.c_str(),url.c_str()); 
    
    tmpurl =  url;
    currentStationName = name;
    playRadio(0);
    server.send(200, "application/json", "{\"playing\":0}");
    return;
    
/*    
    for (int i = 0; i < radioData.count; i++) {
      if (radioData.radios[i].url == url) {
        playRadio(i);
        server.send(200, "application/json", "{\"playing\":" + String(i) + "}");
        return;
      }
    }
    
    if (radioData.count < 10) {
      int tempId = radioData.count;
      radioData.radios[tempId].id = tempId;
      radioData.radios[tempId].name = name;
      radioData.radios[tempId].url = url;
      radioData.count++;
      saveRadios();
      playRadio(tempId);
      server.send(200, "application/json", "{\"playing\":" + String(tempId) + "}");
    } else {
      playRadio(0);
      server.send(200, "application/json", "{\"playing\":0}");
    }
*/
    
  } else {
    server.send(400, "application/json", "{\"error\":\"Missing parameters\"}");
  }
}

void handleSearch() {
  String lang = "";
  bool isChinese = false;
  if (server.hasHeader("Accept-Language")) {
    lang = server.header("Accept-Language");
    isChinese = (lang.indexOf("zh") != -1);
  }

  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>";
  html += isChinese ? "搜尋電台" : "Search Stations";
  html += "</title><style>";
  html += "body{font-family:Arial;margin:10px;background:#1a1a1a;color:#fff}";
  html += ".card{background:#2a2a2a;padding:15px;margin:10px 0;border-radius:10px}";
  html += ".btn{display:inline-block;padding:8px 15px;margin:5px;background:#4CAF50;color:#fff;text-decoration:none;border-radius:5px;font-size:12px}";
  html += ".btn-back{background:#666}.btn-add{background:#2196F3}.btn-play{background:#FF9800}";
  html += "select{padding:8px;margin:5px;background:#444;color:#fff;border:1px solid #666;border-radius:4px}";
  html += ".station-list{list-style:none;padding:0}";
  html += ".station-item{background:#333;padding:10px;margin:5px 0;border-radius:5px}";
  html += ".station-name{font-weight:bold;margin-bottom:5px}";
  html += ".station-info{font-size:11px;color:#aaa;margin-bottom:5px}";
  html += ".loading{text-align:center;padding:20px;color:#888}";
  html += ".now-playing{background:#FF9800;color:#000;padding:10px;margin:10px 0;border-radius:5px;text-align:center;font-weight:bold}";
  html += "</style></head><body>";
  html += "<h1>" + String(isChinese ? "搜尋電台" : "Search Stations") + "</h1>";
  html += "<a href='/' class='btn btn-back'>" + String(isChinese ? "回首頁" : "Back") + "</a>";
  html += "<a href='javascript:saveCurrent()' class='btn btn-add' id='saveBtn' style='display:none'>" + String(isChinese ? "儲存電台" : "Save Station") + "</a>";
  
  html += "<div id='nowPlaying' class='now-playing' style='display:none'></div>";
  
  html += "<div class='card'>";
  html += "<label>" + String(isChinese ? "國家:" : "Country:") + "</label>";
  html += "<select id='country'>";
  for (int i = 0; i < COUNTRY_COUNT; i++) {
    html += "<option value='" + String(COUNTRIES[i].code) + "'>" + String(COUNTRIES[i].name) + "</option>";
  }
  html += "</select>";
  
  html += "<label>" + String(isChinese ? "類型:" : "Genre:") + "</label>";
  html += "<select id='genre'>";
  for (int i = 0; i < GENRE_COUNT; i++) {
    html += "<option value='" + String(GENRES[i].tag) + "'>" + String(GENRES[i].label) + "</option>";
  }
  html += "</select>";
  
  html += "<button class='btn' onclick='searchStations(0)'>" + String(isChinese ? "搜尋" : "Search") + "</button>";
  html += "</div>";
  
  html += "<div id='results' class='station-list'></div>";
  html += "<div id='loading' class='loading' style='display:none'>Loading...</div>";
  html += "<div id='more' style='text-align:center;display:none'>";
  html += "<button class='btn' onclick='loadMore()'>" + String(isChinese ? "載入更多" : "Load More") + "</button>";
  html += "</div>";

  html += "<script>";
  html += "let currentPage = 0;";
  html += "let currentCountry = 'all';";
  html += "let currentGenre = 'all';";
  html += "let currentStation = null;";
  html += "let isZh = window.navigator.language.includes('zh');";
  html += "function searchStations(page) {";
  html += "  currentPage = page;";
  html += "  currentCountry = document.getElementById('country').value;";
  html += "  currentGenre = document.getElementById('genre').value;";
  html += "  document.getElementById('loading').style.display = 'block';";
  html += "  document.getElementById('more').style.display = 'none';";
  html += "  if(page == 0) document.getElementById('results').innerHTML = '';";
  html += "  fetch('/api/stations?country=' + currentCountry + '&genre=' + currentGenre + '&page=' + page)";
  html += "    .then(r => r.json())";
  html += "    .then(data => {";
  html += "      document.getElementById('loading').style.display = 'none';";
  html += "      const list = document.getElementById('results');";
  html += "      data.forEach(s => {";
  html += "        const li = document.createElement('li');";
  html += "        li.className = 'station-item';";
  html += "        li.innerHTML = '<div class=\"station-name\">' + s.name + '</div>' +";
  html += "          '<div class=\"station-info\">' + s.country +' | '+s.codec+ ' | ' + s.bitrate + ' kbps</div>' +";
  html += String("          '<a href=\"javascript:playStation(\\'' + encodeURIComponent(s.name) + '\\',\\'' + encodeURIComponent(s.url) + '\\')\" class=\"btn btn-play\">▶ ") + (isChinese ? "播放" : "Play") + "</a>' +";
  html += String("          '<a href=\"javascript:addStation(\\'' + encodeURIComponent(s.name) + '\\',\\'' + encodeURIComponent(s.url) + '\\')\" class=\"btn btn-add\">+") + (isChinese ? "儲存" : "Save") + "</a>';";
  html += "        list.appendChild(li);";
  html += "      });";
  html += "      if(data.length >= 20) document.getElementById('more').style.display = 'block';";
  html += "    })";
  html += "    .catch(e => {";
  html += "      document.getElementById('loading').style.display = 'none';";
  html += "      alert('Search failed: ' + e);";
  html += "    });";
  html += "}";
  html += "function loadMore() { searchStations(currentPage + 1); }";
  html += "function playStation(name, url) {";
  html += "  currentStation = {name: name, url: url};";
  html += "  fetch('/api/temp-play?name=' + encodeURIComponent(name) + '&url=' + encodeURIComponent(url));";
  html += "  document.getElementById('nowPlaying').style.display = 'block';";
  html += "  document.getElementById('nowPlaying').innerText = '▶ ' + (isZh ? '正在播放:' : 'Playing:') + name;";
  html += "  document.getElementById('saveBtn').style.display = 'inline-block';";
  html += "}";
  html += "function addStation(name, url) {";
  html += "  if(confirm((isZh ? '新增電台?' : 'Add station?') + '\\n' + name)) {";
  html += "    fetch('/api/add-station?name=' + encodeURIComponent(name) + '&url=' + encodeURIComponent(url))";
  html += "      .then(r => r.json())";
  html += "      .then(d => alert(d.success ? (isZh ? '已新增' : 'Added!') : (isZh ? '失敗' : 'Failed')));";
  html += "  }";
  html += "}";
  html += "function saveCurrent() {";
  html += "  if(!currentStation) return;";
  html += "  addStation(currentStation.name, currentStation.url);";
  html += "}";
  html += "</script></body></html>";
  
  server.send(200, "text/html; charset=utf-8", html);
}

void startSmartConfig() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x10_tr);
  u8g2.setCursor(0, ROW[1]);
  u8g2.print("SmartConfig Waiting...");
  u8g2.setCursor(0, ROW[2]);
  u8g2.print("Use Esptouch App");
  u8g2.sendBuffer();

  WiFi.beginSmartConfig();

  Serial.println("Waiting for SmartConfig...");
  while (!WiFi.smartConfigDone()) {
    delay(500);
    Serial.print("#");
  }
  Serial.println("\nSmartConfig Received.");
  
  // 等待自動連線成功
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  ESP.restart();
}

void setup() {
  delay(500);
  Serial.begin(115200);
  randomSeed(millis());
  
  initFS();
  listSPIFFSFiles();  //列出SPIFFS裡的檔案有哪一些
  
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
//如果沒連上網路進行自動配網設定,手機須要安裝ESPtouch App
  if (WiFi.status() != WL_CONNECTED) {
    startSmartConfig();
  }
 
  auto config = i2s.defaultConfig(TX_MODE);
  config.pin_bck = I2S_BCK;
  config.pin_ws = I2S_LRC;
  config.pin_data = I2S_DOUT;
//  config.channels = 1;
  i2s.begin(config);
  
  volume.begin(config);
  volume.setVolume(0.9);
//  reducer.begin(config);
  
  decStream.begin();
  copier.begin(decStream, urlStream);  

  server.collectHeaders(headerKeys, headerKeysCount);  
  server.on("/", HTTP_GET, handleRoot);
  server.on("/edit", HTTP_GET, handleEdit);
  server.on("/search", HTTP_GET, handleSearch);
  server.on("/api/stations", HTTP_GET, handleApiStations);
  server.on("/api/add-station", HTTP_GET, handleApiAddStation);
  server.on("/api/temp-play", HTTP_GET, handleApiTempPlay);
  server.on("/play/0", HTTP_GET, handleStop);
  server.on("/play", HTTP_GET, []() {
    if (server.hasArg("id")) {
      int id = server.arg("id").toInt();       
//      Serial.printf("play id from web: %d",id);  
      playRadio(id);
    }
    server.sendHeader("Location", "/");
    server.send(302);
  });
  server.on("/volume", HTTP_POST, handleVolume);
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
 //   Serial.print("IP: ");
 //   Serial.println(WiFi.localIP());
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
//    if (isPlaying) {
//        copier.copy();
//    }
    if (isPlaying) {
        size_t len = copier.copy();
        
        if (len > 0) {
            lastDataMillis = millis(); // 只要有資料，就更新時間
        } else {
            // 如果超過 10秒沒抓到新數據，強制重新連線
            if (millis() - lastDataMillis > 10000) {
                Serial.println("Stream timed out. Reconnecting...");
                playRadio(radioData.current); 
            } else if (urlStream.httpRequest().connected() == false){                
//            } else if (!urlStream.httpRequest()){                
                Serial.println("HTTP disconnected");
                playRadio(radioData.current);              
            } else if (WiFi.status() != WL_CONNECTED){
                Serial.println("WiFi lost");
                playRadio(radioData.current);              
            }            
        }        
    }

    // 每 5 次循環才處理一次 WebServer，減少對音訊的干擾
    static int webCount = 0;
    if (webCount++ > 5) {
        server.handleClient();
        webCount = 0;
    }
  
  if (millis() - lastEQUpdate > 66) {
    lastEQUpdate = millis();
    if (WiFi.status() == WL_CONNECTED) {
      updateDisplay();
    }
  }

//  if (millis() - lastDisplayUpdate > 1000) {
//    lastDisplayUpdate = millis();
//    Serial.printf("Heap: %d(%d)\n", ESP.getFreeHeap(),ESP.getMaxAllocHeap());
//  }  
}
