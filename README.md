# 🛡️ IoT Smart Room Security & Environment Alert
Proyek Akhir Selection Phase 2 IoT - **Samsung Innovation Campus (SIC) Batch 8**.

## 📌 Tentang Proyek
Sistem monitoring dan keamanan ruangan pintar berbasis ESP32 (Wokwi) yang mengirimkan data sensor secara real-time menggunakan protokol **MQTT** ke broker publik HiveMQ, lalu divisualisasikan melalui web dashboard interaktif berbasis **Streamlit (Python)**.

## 🛠️ Komponen Hardware
- ESP32 DevKit V4
- Sensor DHT22 (Suhu & Kelembapan)
- Sensor PIR (Pendeteksi Gerakan)
- Sensor LDR (Cahaya)
- OLED Display SSD1306
- Buzzer & LED

## 🚀 Cara Menjalankan
1. Buka simulasi Wokwi dan masukkan kode dari folder `firmware/sketch.ino`.
2. Jalankan dashboard Python lokal di komputer:
   ```bash
   pip install -r dashboard/requirements.txt
   streamlit run dashboard/app.py