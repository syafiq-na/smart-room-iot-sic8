import json
import queue
import threading
import time
import paho.mqtt.client as mqtt
import pandas as pd
import streamlit as st

# Konfigurasi Tampilan Web Dashboard
st.set_page_config(
    page_title="Smart Room Dashboard - Syafiq", page_icon="🛡️", layout="wide"
)

st.title("🛡️ Smart Room Telemetry Dashboard (Local)")
st.markdown(
    "Monitoring real-time ESP32 Wokwi menggunakan HiveMQ MQTT Broker (TCP Port"
    " 1883)."
)


# 1. Gunakan st.cache_resource agar Queue & MQTT Client bersifat Singleton (survive antar rerun)
@st.cache_resource
def get_mqtt_queue():
  return queue.Queue(maxsize=100)


data_queue = get_mqtt_queue()


# 2. Fungsi background thread yang aman
def background_mqtt_worker():
  def on_message(client, userdata, msg):
    try:
      payload_str = msg.payload.decode("utf-8")
      data = json.loads(payload_str)
      data_queue.put(data)  # Masukkan data ke thread-safe queue
    except Exception as ex:
      print("Parse error:", ex)

  try:
    client = mqtt.Client(client_id="Syafiq_Local_Client_V4")
    client.on_message = on_message
    client.connect("broker.hivemq.com", 1883, 60)
    client.subscribe("sic8/smartroom/syafiq/data")
    client.loop_start()
  except Exception as e:
    print("MQTT init error:", e)


# 3. Jalankan background worker sekali saja saat aplikasi pertama kali dicheck
@st.cache_resource
def init_mqtt_thread():
  t = threading.Thread(target=background_mqtt_worker, daemon=True)
  t.start()
  return True


init_mqtt_thread()

# 4. Inisialisasi data history di session_state Streamlit agar bertahan di layar
if "data_history" not in st.session_state:
  st.session_state.data_history = []

# 5. Ambil data dari queue secara aman dan masukkan ke session_state
while not data_queue.empty():
  item = data_queue.get()
  st.session_state.data_history.append(item)
  # Batasi maksimal 30 data terakhir agar grafik tidak terlalu panjang
  if len(st.session_state.data_history) > 30:
    st.session_state.data_history.pop(0)

# --- LAYOUT DASHBOARD ---
if len(st.session_state.data_history) > 0:
  latest = st.session_state.data_history[-1]

  # Kotak Metrik Atas (Card Style)
  col1, col2, col3, col4 = st.columns(4)
  col1.metric("Status Ruangan", latest.get("status", "AMAN"))
  col2.metric("Suhu", f"{latest.get('suhu', 0)} °C")
  col3.metric("Kelembapan", f"{latest.get('kelembapan', 0)} %")
  col4.metric("Cahaya (LDR)", latest.get("cahaya", 0))

  st.markdown("---")

  # Grafik Tren Interaktif
  df = pd.DataFrame(st.session_state.data_history)
  st.subheader("📈 Grafik Tren Suhu & Kelembapan Real-time")
  st.line_chart(df, y=["suhu", "kelembapan"])
else:
  st.warning(
      "⏳ Menunggu data masuk dari Wokwi... Pastikan simulasi Wokwi sedang"
      " berjalan (Play) dan terhubung ke broker MQTT."
  )

# Auto-refresh halus setiap 2 detik tanpa merusak socket browser
time.sleep(2)
st.rerun()