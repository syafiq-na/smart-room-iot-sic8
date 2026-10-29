/*
  Smart Room Security & Environment Alert
  Samsung Innovation Campus Batch 8, Selection Phase 2 IoT

  Controller : ESP32 DevKit C
  Input      : PIR (gerakan), LDR (cahaya), DHT22 (suhu dan kelembapan)
  Output     : LED (lampu otomatis), Buzzer (sirine), OLED SSD1306 (status)
  Komunikasi : Serial Monitor dan MQTT ke HiveMQ (TCP, port 1883)
*/

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// =====================================================================
// 1. KONFIGURASI PIN
// =====================================================================
const uint8_t PIN_PIR    = 27;   
const uint8_t PIN_LDR    = 34;   
const uint8_t PIN_DHT    = 4;    
const uint8_t PIN_LED    = 26;   
const uint8_t PIN_BUZZER = 25;   
const uint8_t PIN_SDA    = 21;   
const uint8_t PIN_SCL    = 22;   

// =====================================================================
// 2. PARAMETER SISTEM
// =====================================================================
const int   AMBANG_GELAP            = 2000;  
const bool  LDR_GELAP_NILAI_TINGGI  = true;  
const float AMBANG_SUHU             = 35.0;  

const unsigned long LED_TAHAN_MS        = 10000;  
const unsigned long INTERVAL_SENSOR_MS  = 2000;   
const unsigned long INTERVAL_KIRIM_MS   = 5000;   
const unsigned long INTERVAL_OLED_MS    = 500;    
const unsigned long INTERVAL_MQTT_MS    = 3000;
const unsigned long INTERVAL_WIFI_MS    = 15000;  

// =====================================================================
// 3. WIFI DAN MQTT
// =====================================================================
const char* WIFI_SSID     = "Wokwi-GUEST";
const char* WIFI_PASS     = "";
const int   WIFI_CHANNEL  = 6;

const char*    MQTT_HOST      = "broker.hivemq.com";
const uint16_t MQTT_PORT      = 1883;
const char*    MQTT_USER      = "";
const char*    MQTT_PASS      = "";
// Client ID dibuat unik saat boot agar beberapa simulasi Wokwi tidak
// saling memutus koneksi pada broker publik.
char mqttClientId[48];

const char* TOPIC_DATA   = "sic8/smartroom/syafiq/data";
const char* TOPIC_STATUS = "sic8/smartroom/syafiq/status";
const char* TOPIC_ALERT  = "sic8/smartroom/syafiq/alert";

const bool mqttDiatur = (strncmp(MQTT_HOST, "ISI_", 4) != 0);

// =====================================================================
// 4. OBJEK DAN VARIABEL GLOBAL
// =====================================================================
enum StatusRuangan { AMAN, WASPADA, ALARM_GERAKAN, SUHU_TINGGI };
const char* NAMA_STATUS[] = {"AMAN", "WASPADA", "ALARM GERAKAN", "SUHU TINGGI"};
const char* LABEL_OLED[]  = {"AMAN", "WASPADA", "ALARM", "SUHU TINGGI"};

DHT dht(PIN_DHT, DHT22);
Adafruit_SSD1306 display(128, 64, &Wire, -1);
WiFiClient espClient;
PubSubClient mqtt(espClient);

bool oledOK = false;
StatusRuangan status = AMAN;
unsigned long tStatusBerubah = 0;

float suhu = 0.0;
float kelembapan = 0.0;
int   nilaiLDR = 0;
bool  gelap = false;
bool  gerakan = false;
bool  pernahGerakan = false;
unsigned long tGerakanTerakhir = 0;

bool buzzerNyala = false;
int  buzzerFreq = 0;
unsigned long tSensor = 0, tKirim = 0, tOled = 0, tCobaMqtt = 0, tCobaWifi = 0;

// =====================================================================
// 5. BUZZER
// =====================================================================
#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif
const uint8_t BUZZER_CH = 0;

void buzzerInit() {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(PIN_BUZZER, 2000, 8);
#else
  ledcSetup(BUZZER_CH, 2000, 8);
  ledcAttachPin(PIN_BUZZER, BUZZER_CH);
#endif
}

void buzzerNada(int frekuensi) {
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWriteTone(PIN_BUZZER, frekuensi);
#else
  ledcWriteTone(BUZZER_CH, frekuensi);
#endif
}

// =====================================================================
// 6. KONEKSI WIFI DAN MQTT
// =====================================================================
void sambungWiFi() {
  Serial.print("Menghubungkan WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS); // Tanpa channel agar melakukan auto-scan otomatis
  
  unsigned long mulai = millis();
  // Perpanjang timeout menjadi 15 detik agar ESP32 punya cukup waktu saat booting
  while (WiFi.status() != WL_CONNECTED && millis() - mulai < 15000) {
    delay(500);
    Serial.print(".");
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" terhubung!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println(" gagal, sistem berjalan tanpa jaringan");
  }
}

void kirimStatus() {
  if (mqtt.connected()) {
    mqtt.publish(TOPIC_STATUS, NAMA_STATUS[status], true);
  }
}

void kirimAlert(const char* pesan) {
  if (mqtt.connected()) {
    mqtt.publish(TOPIC_ALERT, pesan);
    Serial.print("Publish ");
    Serial.print(TOPIC_ALERT);
    Serial.print(": ");
    Serial.println(pesan);
  }
}

void kirimData() {
  if (!mqtt.connected()) {
    Serial.println("Gagal kirim: MQTT belum tersambung ke broker!");
    return;
  }

  char payload[180];
  snprintf(payload, sizeof(payload),
           "{\"suhu\":%.1f,\"kelembapan\":%.1f,\"cahaya\":%d,\"gerakan\":%d,\"status\":\"%s\"}",
           suhu, kelembapan, nilaiLDR, gerakan ? 1 : 0, NAMA_STATUS[status]);

  mqtt.publish(TOPIC_DATA, payload);
  Serial.print("Berhasil Publish ke MQTT: ");
  Serial.println(payload);
}
void sambungMQTT() {
  if (!mqttDiatur || WiFi.status() != WL_CONNECTED || mqtt.connected()) return;
  if (tCobaMqtt != 0 && millis() - tCobaMqtt < INTERVAL_MQTT_MS) return;
  tCobaMqtt = millis();

  Serial.print("Menghubungkan MQTT... ");
  if (mqtt.connect(mqttClientId, MQTT_USER, MQTT_PASS, TOPIC_STATUS, 0, true, "OFFLINE")) {
    Serial.println("MQTT terhubung ke HiveMQ Broker");
    kirimStatus();
  } else {
    Serial.print("gagal, kode: ");
    Serial.println(mqtt.state());
  }
}

// =====================================================================
// 7. PEMBACAAN SENSOR DAN LOGIKA STATUS
// =====================================================================
void bacaSensor() {
  long total = 0;
  for (int i = 0; i < 8; i++) {
    total += analogRead(PIN_LDR);
    delay(2);
  }
  nilaiLDR = total / 8;
  gelap = LDR_GELAP_NILAI_TINGGI ? (nilaiLDR >= AMBANG_GELAP) : (nilaiLDR <= AMBANG_GELAP);

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  if (!isnan(t) && !isnan(h)) {
    suhu = t;
    kelembapan = h;
  }
}

StatusRuangan hitungStatus() {
  if (gerakan && gelap)       return ALARM_GERAKAN;
  if (suhu >= AMBANG_SUHU)    return SUHU_TINGGI;
  if (gerakan)                return WASPADA;
  return AMAN;
}

void ubahStatus(StatusRuangan baru) {
  status = baru;
  tStatusBerubah = millis();
  Serial.print(">> Status berubah: ");
  Serial.println(NAMA_STATUS[status]);
  kirimStatus();

  if (baru == ALARM_GERAKAN) {
    kirimAlert("Gerakan terdeteksi saat ruangan gelap");
  } else if (baru == SUHU_TINGGI) {
    char pesan[64];
    snprintf(pesan, sizeof(pesan), "Suhu ruangan tinggi: %.1f C", suhu);
    kirimAlert(pesan);
  }
}

void cetakSerial() {
  Serial.print("Suhu: ");
  Serial.print(suhu, 1);
  Serial.print(" C | Kelembapan: ");
  Serial.print(kelembapan, 1);
  Serial.print(" % | Cahaya: ");
  Serial.print(nilaiLDR);
  Serial.print(" | Gerakan: ");
  Serial.print(gerakan ? 1 : 0);
  Serial.print(" | Status: ");
  Serial.println(NAMA_STATUS[status]);
}

// =====================================================================
// 8. OUTPUT: LED, BUZZER, OLED
// =====================================================================
void updateLED(unsigned long now) {
  bool nyala = gelap && pernahGerakan && (now - tGerakanTerakhir < LED_TAHAN_MS);
  digitalWrite(PIN_LED, nyala ? HIGH : LOW);
}

void updateBuzzer(unsigned long now) {
  bool bunyi = false;
  int freq = 0;

  switch (status) {
    case ALARM_GERAKAN:
      bunyi = (now % 500) < 250;
      freq = 2200;
      break;
    case SUHU_TINGGI:
      bunyi = (now % 2000) < 800;
      freq = 1000;
      break;
    case WASPADA:
      bunyi = (now - tStatusBerubah) < 200;
      freq = 1500;
      break;
    default:
      bunyi = false;
  }

  if (bunyi && (!buzzerNyala || freq != buzzerFreq)) {
    buzzerNada(freq);
    buzzerNyala = true;
    buzzerFreq = freq;
  } else if (!bunyi && buzzerNyala) {
    buzzerNada(0);
    buzzerNyala = false;
  }
}

void tampilOLED() {
  if (!oledOK) return;
  char baris[28];

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print("SMART ROOM ALERT");

  const char* label = LABEL_OLED[status];
  display.fillRect(0, 12, 128, 18, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(strlen(label) <= 10 ? 2 : 1);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(label, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((128 - w) / 2, 12 + (18 - h) / 2);
  display.print(label);

  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  snprintf(baris, sizeof(baris), "Suhu:%.1fC Hum:%.0f%%", suhu, kelembapan);
  display.setCursor(0, 36);
  display.print(baris);

  snprintf(baris, sizeof(baris), "Cahaya:%d %s", nilaiLDR, gelap ? "GELAP" : "TERANG");
  display.setCursor(0, 46);
  display.print(baris);

  const char* mqttLabel = !mqttDiatur ? "OFF" : (mqtt.connected() ? "OK" : "...");
  snprintf(baris, sizeof(baris), "Gerak:%s MQTT:%s", gerakan ? "YA" : "TDK", mqttLabel);
  display.setCursor(0, 56);
  display.print(baris);

  display.display();
}

// =====================================================================
// 9. SETUP DAN LOOP
// =====================================================================
void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== Smart Room Security & Environment Alert ===");

  pinMode(PIN_PIR, INPUT);
  pinMode(PIN_LDR, INPUT);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);
  buzzerInit();
  buzzerNada(0);

  dht.begin();
  Wire.begin(PIN_SDA, PIN_SCL);
  oledOK = display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!oledOK) {
    Serial.println("OLED tidak ditemukan");
  }

  sambungWiFi();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setKeepAlive(30);
  snprintf(mqttClientId, sizeof(mqttClientId), "esp32_smartroom_%llX",
           (unsigned long long)ESP.getEfuseMac());

  bacaSensor();
  cetakSerial();
  tSensor = millis();
  tKirim = millis() - INTERVAL_KIRIM_MS;
  tampilOLED();
}

void loop() {
  unsigned long now = millis();

  // Layani koneksi MQTT sesering mungkin agar keep-alive tidak terlewat
  // ketika sensor atau output sedang diperbarui.
  if (mqtt.connected()) {
    mqtt.loop();
  }

  gerakan = (digitalRead(PIN_PIR) == HIGH);
  if (gerakan) {
    tGerakanTerakhir = now;
    pernahGerakan = true;
  }

  if (now - tSensor >= INTERVAL_SENSOR_MS) {
    tSensor = now;
    bacaSensor();
    cetakSerial();
  }

  StatusRuangan baru = hitungStatus();
  if (baru != status) {
    ubahStatus(baru);
    tOled = 0;
  }

  updateLED(now);
  updateBuzzer(now);
  if (now - tOled >= INTERVAL_OLED_MS) {
    tOled = now;
    tampilOLED();
  }

  if (WiFi.status() != WL_CONNECTED) {
    if (now - tCobaWifi >= INTERVAL_WIFI_MS) {
      tCobaWifi = now;
      WiFi.reconnect();
    }
  } else {
    sambungMQTT();
    if (mqtt.connected() && now - tKirim >= INTERVAL_KIRIM_MS) {
      tKirim = now;
      kirimData();
    }
  }
}