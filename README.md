# StreamRadio
<br>
<b>2026.04.16 by Halin</b><br>
<br>
<size=2></h2>(超實用)Stream audio by ESP32 c3 super mini(必安裝)<size=1><br>
<br>
使用ESP32 C3 super mini + Max98357a + SSD1306 I2C 128*64<br>
解碼函式庫採用arduino-audio-tools-1.2.2(須安裝)<br>
功能:<br>
  1.從web server設定電台並操作新增刪除編輯播放停止<br>
  2.電台串流格式支援MP3以及AAC兩種<br>
設置:<br>
  1.第一次執行或找不到網路時將自動進入SmartConfig等待SSID帳號密碼<br>
  2.手機下載ESPtouch掃描裝置自動分享SSID以及密碼給ESP32後自動重開機<br>
  3.開啟同區域網路的手機平板電腦瀏覽器開啟ESP32的IP後設置電台即可<br>
  
***************************************************************<br>
注意事項<br>
ESP32 C3 Super mini單核心且可用記憶體很少,使用在解碼串流很吃力<br>
如果要再增加不同格式串流須要特別注意heap的使用狀況<br>
<b>下載記得一定要給星..^.^..</b><br>
***************************************************************<br>
<a href="https://youtube.com/shorts/peeemmVe75Y" target="_blank">
    <img src="https://youtube.com/shorts/peeemmVe75Y/0.jpg" 
         alt="Stream audio" 
         width="50%" height="auto">
</a>
[![Watch the video](http://youtube.com)](https://youtube.com/shorts/peeemmVe75Y)
<br><br>
<b>2026.04.22 Version 2.0</b><br>
<br>
 Search internet radio url from de1.api.radio-browser.info<br>
 StreamRadio_v2.ino<br>
