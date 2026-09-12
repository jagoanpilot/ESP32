# Votol BLE + WiFi + TFT Dashboard

[![Author](https://img.shields.io/badge/Author-Zekri%20R-blue)](https://zekri.id)
[![Platform](https://img.shields.io/badge/Platform-ESP32-green)](https://www.espressif.com/)
[![License](https://img.shields.io/badge/License-MIT-yellow)](LICENSE)

Real-time monitoring dashboard untuk **Votol Controller**, **BMS (Battery Management System)**, dan sistem baterai kendaraan listrik melalui **CAN Bus**.

Project ini menggunakan:

* **ESP32**
* **SN65HVD230 CAN Transceiver**
* **ILI9488 TFT 480×320**
* BLE
* WiFi
* OTA Firmware Update
* CAN Bus
* Votol Controller
* BMS

Firmware ESP32 bekerja dengan arsitektur **Dual-Core**:

* **Core 0** → CAN Bus processing dan real-time vehicle data
* **Core 1** → BLE, WiFi, WebSocket, OTA, dan komunikasi dashboard

---

# ⚠️ DISCLAIMER / PERINGATAN

> **ENGLISH:**
> This project is provided "AS IS" without any warranty. Use at your own risk.
>
> The author (**Zekri R / ZEKRI.ID**) is **NOT responsible** for any damage, malfunction, or injury that may occur to your vehicle, controller, battery, or any other components.
>
> Modifying or monitoring an electric vehicle CAN bus may void your warranty and could be dangerous if done incorrectly.

> **BAHASA INDONESIA:**
> Proyek ini disediakan "APA ADANYA" tanpa jaminan apapun. Gunakan dengan risiko Anda sendiri.
>
> Penulis (**Zekri R / ZEKRI.ID**) **TIDAK bertanggung jawab** atas kerusakan, malfungsi, atau cedera yang mungkin terjadi pada kendaraan, controller, baterai, atau komponen lainnya.
>
> Memodifikasi atau memonitor CAN Bus kendaraan listrik dapat membatalkan garansi dan dapat berbahaya jika dilakukan dengan tidak benar.

---

# 🎁 Donasi / Support

Jika project ini bermanfaat bagi Anda, Anda bisa mendukung pengembangan selanjutnya dengan berdonasi melalui QRIS berikut:

<p align="center">
  <img src="QRIS.jpg" alt="QRIS Zekri R" width="200">
</p>

Terima kasih atas dukungannya! 🙏

---

# 📋 Deskripsi Project

Sistem monitoring real-time untuk kendaraan listrik berbasis **ESP32 + CAN Bus**.

ESP32 membaca data dari:

* Votol Controller
* BMS
* Charger
* Battery Pack

Data kemudian dapat ditampilkan melalui:

1. **TFT ILI9488**
2. **BLE**
3. **WiFi WebSocket**
4. **Go Desktop Dashboard**
5. **Android App**

Sistem juga menyediakan:

* Odometer
* Trip meter
* RPM
* Speed
* Battery voltage
* Battery current
* Power input
* Power output
* Regen monitoring
* Battery SOC
* Battery SOH
* Temperature monitoring
* Individual cell monitoring
* Charging monitoring
* OTA firmware update

---

# 📊 Data yang Dimonitor

| Kategori             | Data                                                |
| -------------------- | --------------------------------------------------- |
| **Votol Controller** | RPM, Speed, Mode                                    |
| **Vehicle Mode**     | PARK, STAND, DRIVE, SPORT, REVERSE, BRAKE, CHARGING |
| **Controller**       | Controller Temperature                              |
| **Motor**            | BLDC Motor Temperature                              |
| **Battery**          | Pack Voltage, Current, SOC                          |
| **Power**            | Watt IN, Watt OUT                                   |
| **Energy Recovery**  | Regenerative Braking                                |
| **BMS**              | SOC, SOH, Cycle Count                               |
| **Capacity**         | Remaining Capacity, Full Charge Capacity            |
| **Cell Voltage**     | 23 Individual Cell Voltages                         |
| **Cell Analysis**    | Highest, Lowest, Average, Delta                     |
| **Cell Temperature** | 5 Cell Temperature Sensors                          |
| **Balance**          | Balance Mode, Status, Bitmask                       |
| **Charging**         | Charger Voltage, Charger Current, Charging Status   |
| **Charger**          | Original Charger Detection                          |
| **BMS Information**  | Hardware Version, Firmware Version                  |
| **Vehicle History**  | Odometer, Trip Meter                                |

---

# 🔧 Hardware

## 1. ESP32

Board yang digunakan:

**ESP32 Dev Module**

CAN Bus menggunakan peripheral **TWAI** internal ESP32.

---

# 🔌 Wiring ESP32 → SN65HVD230

Gunakan SN65HVD230 sebagai CAN transceiver.

```text
ESP32 DevKit                 SN65HVD230
┌───────────────┐            ┌───────────────┐
│               │            │               │
│ GPIO21  TX ───┼───────────►│ CTX / TXD     │
│ GPIO22  RX ───┼───────────►│ CRX / RXD     │
│               │            │               │
│ 3.3V ─────────┼───────────►│ VCC           │
│ GND ──────────┼───────────►│ GND           │
│               │            │               │
└───────────────┘            │ CANH ─────────┼──────► Votol/BMS CANH
                             │ CANL ─────────┼──────► Votol/BMS CANL
                             └───────────────┘
```

### Pin CAN

| ESP32  | SN65HVD230 | Fungsi   |
| ------ | ---------- | -------- |
| GPIO21 | TXD / CTX  | CAN TX   |
| GPIO22 | RXD / CRX  | CAN RX   |
| 3.3V   | VCC        | Supply   |
| GND    | GND        | Ground   |
| -      | CANH       | CAN High |
| -      | CANL       | CAN Low  |

### CAN Configuration

```text
CAN Speed : 250 kbps
CAN Mode  : TWAI
TX Pin    : GPIO21
RX Pin    : GPIO22
```

> Pastikan CANH dan CANL tidak tertukar.

---

# 🖥️ Wiring ESP32 → ILI9488 TFT

TFT menggunakan interface **SPI**.

```text
ESP32 DevKit                 ILI9488
┌───────────────┐            ┌───────────────┐
│               │            │               │
│ GPIO18 ───────┼───────────►│ SCK / CLK     │
│ GPIO19 ◄──────┼────────────│ MISO          │
│ GPIO23 ───────┼───────────►│ MOSI / DIN    │
│ GPIO27 ───────┼───────────►│ CS            │
│ GPIO26 ───────┼───────────►│ DC / RS       │
│ GPIO25 ───────┼───────────►│ RST / RESET   │
│               │            │               │
│ 3.3V ─────────┼───────────►│ VCC           │
│ GND ──────────┼───────────►│ GND           │
└───────────────┘            └───────────────┘
```

### TFT Pin Mapping

| ESP32 GPIO | ILI9488      | Fungsi         |
| ---------: | ------------ | -------------- |
|     GPIO18 | SCK / CLK    | SPI Clock      |
|     GPIO19 | MISO / T_OUT | SPI Data Out   |
|     GPIO23 | MOSI / T_DIN | SPI Data In    |
|     GPIO27 | CS           | Chip Select    |
|     GPIO26 | DC / RS      | Data / Command |
|     GPIO25 | RST          | Reset          |
|       3.3V | VCC          | Supply         |
|        GND | GND          | Ground         |

### SPI Configuration

```text
SCK  = GPIO18
MISO = GPIO19
MOSI = GPIO23
CS   = GPIO27
DC   = GPIO26
RST  = GPIO25
```

> Pin touchscreen seperti `T_IRQ`, `T_OUT`, `T_DIN`, `T_CS`, dan `T_CLK` tidak digunakan dalam implementasi TFT dashboard saat ini.

---

# 🔘 Wiring Push Button

Dashboard memiliki dua tombol.

## Page / BMS Button

```text
ESP32 GPIO32
     │
     │
   BUTTON
     │
     │
    GND
```

Konfigurasi:

```cpp
pinMode(PAGE_BUTTON_PIN, INPUT_PULLUP);
```

### Fungsi

| Tombol        | Fungsi              |
| ------------- | ------------------- |
| Tidak ditekan | Main Dashboard      |
| Ditekan / LOW | BMS Monitoring Page |

Halaman BMS hanya aktif selama tombol GPIO32 ditahan LOW.

---

# 🔘 Trip Reset Button

```text
ESP32 GPIO33
     │
     │
   BUTTON
     │
     │
    GND
```

Konfigurasi:

```cpp
pinMode(TRIP_RESET_BUTTON_PIN, INPUT_PULLUP);
```

### Fungsi

Tekan tombol:

```text
TRIP → 0.0 km
```

Nilai trip kemudian disimpan ke ESP32 Preferences.

---

# 🖥️ TFT Dashboard

Resolusi:

```text
480 × 320 pixel
```

Rotation:

```text
Rotation 1
```

---

# 🚗 Speedometer

Speed ditampilkan di tengah dashboard.

Format:

```text
45
km/h
```

Range display:

```text
0 - 999 km/h
```

Ketika kendaraan berada pada mode PARK:

```text
P
```

ditampilkan pada bagian tengah.

---

# 🔋 Battery SOC

Battery indicator menampilkan:

```text
BATTERY
████████████
85%
```

Warna battery:

|    SOC | Warna  |
| -----: | ------ |
|  < 20% | Red    |
| 20–49% | Yellow |
|  ≥ 50% | Green  |

Saat charging:

```text
⚡
```

ditampilkan pada battery indicator.

---

# ⚡ Power Flow

Dashboard sekarang membedakan:

* **W IN**
* **W OUT**

## Charging

Ketika kendaraan sedang charging:

```text
W IN  = Voltage × Charging Current
W OUT = 0 W
```

Contoh:

```text
BATT: 83.6V
CURRENT: 4.3A

W IN : 359W
W OUT: 0W
```

Artinya energi sedang **masuk ke baterai**.

---

## Motor Consumption

Ketika kendaraan berjalan:

```text
W IN  = 0 W
W OUT = Battery Voltage × Current
```

Contoh:

```text
BATT: 78.4V
CURRENT: 85A

W IN : 0W
W OUT: 6664W
```

Artinya energi sedang **keluar dari baterai menuju sistem penggerak**.

---

## Regenerative Braking

Ketika regenerative braking aktif:

```text
W IN  = Regenerative Power
W OUT = 0 W
```

Contoh:

```text
REGEN

W IN : 1450W
W OUT: 0W
```

Energi hasil regenerative braking dianggap sebagai energi yang kembali masuk ke baterai.

---

# ♻️ Regenerative Braking Indicator

Saat regen aktif, dashboard menampilkan:

```text
REGEN
```

di bawah speedometer.

Regen dideteksi melalui:

```cpp
atomicRegenActive
```

atau kondisi brake dengan power negatif.

---

# 🌡️ Temperature Monitoring

Dashboard menampilkan:

```text
BLDC: 45C
CTRL: 38C
```

Data yang tersedia:

* BLDC Motor Temperature
* Controller Temperature
* Battery Temperature
* BMS Cell Temperature
* Maximum Temperature
* Minimum Temperature

---

# 📊 BMS Monitoring Page

Tekan dan tahan tombol:

```text
GPIO32 → GND
```

untuk membuka BMS page.

Tampilan BMS:

```text
BMS MONITORING & CELLS

Volt : 78.4 V       Curr : 85.2 A
Power: 6670 W       SOH  : 98 %

Min Cell: 3.620 V   Max Cell: 3.640 V
Delta : 0.020 V     Avg Cell: 3.631 V

C01:3.63  C02:3.63  C03:3.64 ...
C07:3.62  C08:3.63  C09:3.64 ...
C13:3.63  C14:3.63  C15:3.64 ...
C19:3.63  C20:3.63  C21:3.63 ...
C23:3.64

Cycle: 125 | Temp Max: 45 C
```

---

# 🔋 23 Cell Monitoring

Firmware mendukung pembacaan hingga:

```text
23 individual cells
```

Setiap cell ditampilkan dalam format:

```text
C01:3.63
C02:3.63
C03:3.64
...
C23:3.64
```

Unit:

```text
Volt
```

---

# 📊 Cell Analysis

Dashboard menghitung/menampilkan:

* Highest Cell Voltage
* Highest Cell Number
* Lowest Cell Voltage
* Lowest Cell Number
* Average Cell Voltage
* Cell Voltage Delta

Contoh:

```text
Min Cell: 3.620 V (#7)
Max Cell: 3.640 V (#15)
Delta   : 0.020 V
Avg Cell: 3.631 V
```

---

# 🌡️ Cell Temperature

Firmware mendukung pembacaan:

```text
5 Cell Temperature Sensors
```

BMS menyediakan:

* Temperature 1
* Temperature 2
* Temperature 3
* Temperature 4
* Temperature 5

Serta:

```text
Maximum Temperature
Minimum Temperature
Temperature Cell Number
```

---

# 🔋 BMS Capacity

Data kapasitas:

```text
Remaining Capacity
Full Charge Capacity
```

digunakan untuk menghitung SOC apabila data kapasitas tersedia.

Formula:

```text
SOC = Remaining Capacity / Full Charge Capacity × 100
```

Jika data kapasitas tidak tersedia, firmware menggunakan SOC dari BMS.

---

# 🔌 Charging Monitoring

Firmware dapat membaca:

```text
Charger Voltage
Charger Current
Charging Status
Original Charger Detection
```

Status charging dapat berasal dari:

* Controller
* BMS charging flag
* Charger CAN message

---

# 🔌 Original Charger Detection

Firmware mendukung deteksi charger original.

Jika charger original terdeteksi dan kendaraan berada pada charging mode, TFT menampilkan:

```text
ORIGINAL CHARGING
```

---

# 🧠 Vehicle Modes

Firmware mengenali beberapa mode kendaraan:

| Mode       | Keterangan           |
| ---------- | -------------------- |
| `PARK`     | Kendaraan park       |
| `STAND`    | Standby              |
| `DRIVE`    | Mode berkendara      |
| `SPORT`    | Mode sport           |
| `REVERSE`  | Mundur               |
| `BRAKE`    | Brake / regenerative |
| `CHARGING` | Charging             |

Mode ditampilkan pada bagian kanan atas TFT.

---

# 📏 Odometer

Odometer ditampilkan pada footer:

```text
ODO:1250km
```

Nilai disimpan menggunakan:

```text
ESP32 Preferences
```

Data odometer disimpan secara berkala sehingga tetap tersedia setelah ESP32 restart.

---

# 📏 Trip Meter

Trip ditampilkan:

```text
TRIP:12.5km
```

Trip dapat di-reset menggunakan tombol GPIO33.

Data trip juga disimpan menggunakan ESP32 Preferences.

---

# ⏱️ Trip Reset

Trip dapat di-reset melalui:

```text
GPIO33 → Button → GND
```

Satu kali tekan akan:

```text
TRIP → 0.0 km
```

Selain tombol fisik, sistem juga memiliki mekanisme reset trip berdasarkan kondisi tertentu pada mode SPORT.

---

# 🚀 Startup Animation

Saat ESP32 dinyalakan, TFT menampilkan:

```text
LOADING

INITIALIZING DASHBOARD

████████████████████

100%
```

Setelah loading selesai, dashboard menjalankan startup animation sebelum masuk ke data kendaraan sebenarnya.

---

# 📡 CAN Bus

Default CAN configuration:

```text
Baud Rate : 250 kbps
TX        : GPIO21
RX        : GPIO22
```

Firmware membaca berbagai CAN frame untuk mendapatkan:

* RPM
* Speed
* Voltage
* Current
* Power
* Controller Mode
* Odometer
* Battery Capacity
* SOC
* SOH
* Cell Voltage
* Cell Temperature
* Charger Status
* BMS Status
* Balance Status
* BMS Version

---

# 🧩 CAN Monitoring / Read-Only Mode

Firmware dapat digunakan sebagai **CAN monitoring system** tanpa mengirim frame injection ke CAN Bus.

Pada konfigurasi read-only:

```cpp
bool shouldInject = false;
```

Dengan demikian ESP32 hanya membaca dan memproses data CAN.

> **PENTING:** CAN injection dapat memengaruhi sistem kendaraan. Jangan mengaktifkan fitur injection sebelum memahami protokol CAN kendaraan dan melakukan pengujian dengan aman.

---

# ⚡ Dual-Core Architecture

Firmware menggunakan dua core ESP32.

## Core 0

Digunakan untuk:

```text
CAN Bus
CAN Parsing
Vehicle State
Real-time Data
CAN Rate
```

## Core 1

Digunakan untuk:

```text
BLE
WiFi
WebSocket
OTA
JSON
Dashboard Communication
```

Arsitektur ini mengurangi bottleneck sehingga pembacaan CAN dan komunikasi dashboard dapat berjalan secara paralel.

---

# 📶 BLE Mode

BLE digunakan sebagai transport utama.

Fitur:

* Auto Connect
* Real-time Data
* Fast Data Stream
* Full Data Snapshot
* Dashboard communication

---

# 🌐 WiFi Mode

ESP32 dapat membuat WiFi Access Point.

SSID:

```text
VOTOL_Dashboard
```

Password:

```text
votol1234
```

IP:

```text
192.168.4.1
```

WebSocket:

```text
Port 81
```

WiFi digunakan terutama untuk:

* Web Dashboard
* OTA Firmware Update
* Real-time monitoring

---

# 🔄 BLE ↔ WiFi

Transport dapat berpindah antara:

```text
BLE
  ↕
WiFi
```

Perpindahan dikelola oleh firmware sehingga komunikasi dashboard tetap dapat berjalan.

---

# 🔄 OTA Firmware Update

Firmware ESP32 mendukung:

```text
Over-The-Air Firmware Update
```

Firmware dapat diperbarui melalui WiFi tanpa harus mencabut ESP32 dari kendaraan.

Endpoint OTA:

```text
POST http://192.168.4.1/update
```

Body:

```text
Raw firmware .bin
```

---

# 📱 Android App

Firmware ESP32 tetap kompatibel dengan aplikasi Android lama.

PEV App:

https://github.com/zexry619/pev-app-release

---

# 💻 Go Desktop Dashboard

Project juga menyediakan aplikasi desktop berbasis Go.

Tidak diperlukan Python.

Platform:

```text
Windows
Linux
macOS
```

Build menghasilkan single binary.

---

# 📥 Download

Download binary dari halaman:

```text
Releases
```

File yang tersedia:

### Windows

```text
votol-dashboard-windows-amd64.exe
```

### Linux PC

```text
votol-dashboard-linux-amd64
```

### Raspberry Pi

```text
votol-dashboard-linux-arm64
```

### macOS Intel

```text
votol-dashboard-darwin-amd64
```

### macOS Apple Silicon

```text
votol-dashboard-darwin-arm64
```

### ESP32

```text
firmware.bin
```

---

# 🔧 Flash Firmware ESP32

Gunakan ESP Web Tools atau esptool.

Contoh:

```bash
esptool.py --chip esp32 \
--port /dev/ttyUSB0 \
write_flash 0x10000 firmware.bin
```

---

# 🛠️ Build from Source

## Go Dashboard

```bash
cd go_web
```

Dengan BLE:

```bash
CGO_ENABLED=1 go build -o votol-dashboard .
```

Tanpa BLE:

```bash
CGO_ENABLED=0 go build -o votol-dashboard .
```

---

# 🔧 ESP32 Firmware

Gunakan:

**Arduino IDE**

Board:

```text
ESP32 Dev Module
```

ESP32 Board Manager:

```text
2.0.17
```

> Versi ESP32 Arduino Core 3.x mungkin tidak kompatibel dengan konfigurasi/library yang digunakan project ini.

Partition Scheme:

```text
Minimal SPIFFS
(1.9MB APP with OTA/190KB SPIFFS)
```

Library:

```text
AsyncTCP
ESPAsyncWebServer
TFT_eSPI
```

Source utama:

```text
esp32/votol_ble_dualcore/votol_ble_dualcore.ino
```

---

# 🖼️ TFT_eSPI Configuration

Pastikan konfigurasi TFT_eSPI sesuai dengan wiring:

```text
TFT Controller : ILI9488

SCK  : GPIO18
MISO : GPIO19
MOSI : GPIO23
CS   : GPIO27
DC   : GPIO26
RST  : GPIO25
```

---

# 📊 JSON Data Format

ESP32 mengirim data ringkas untuk menghemat RAM dan bandwidth.

Contoh:

```json
{
  "v": 78.4,
  "a": 85.2,
  "r": 1250,
  "s": 45,
  "m": "SPORT",
  "p": 6670,
  "sc": 85,
  "t": {
    "c": 38,
    "m": 45,
    "b": 35
  }
}
```

Keterangan:

| Field | Data                   |
| ----- | ---------------------- |
| `v`   | Battery Voltage        |
| `a`   | Battery Current        |
| `r`   | RPM                    |
| `s`   | Speed                  |
| `m`   | Vehicle Mode           |
| `p`   | Battery Power          |
| `sc`  | SOC                    |
| `t.c` | Controller Temperature |
| `t.m` | Motor Temperature      |
| `t.b` | Battery Temperature    |

---

# ⚡ Power Flow Data

Dashboard membedakan:

```text
W IN
W OUT
```

Secara konsep:

```text
Charging:

Battery
   ▲
   │
 W IN
   │
Charger
```

Saat berkendara:

```text
Battery
   │
 W OUT
   ▼
Controller
   │
   ▼
Motor
```

Saat regen:

```text
Motor
   │
   │ Regen
   ▼
Battery
   ▲
 W IN
```

---

# 📖 Protocol Documentation

Dokumentasi protokol:

```text
docs/BLE_WIFI_PROTOCOL.md
```

---

# 🔧 Troubleshooting

## ESP32 Tidak Terdeteksi via USB

Periksa:

* Driver CP210x / CH340
* Kabel USB harus mendukung data
* COM port tidak sedang digunakan aplikasi lain

---

## Data CAN Tidak Muncul

Periksa:

```text
CANH → CANH
CANL → CANL
```

Jangan sampai:

```text
CANH ↔ CANL
```

terbalik.

Periksa juga:

```text
CAN Baud Rate = 250 kbps
```

Pastikan controller dan BMS menggunakan baud rate yang sesuai.

---

## TFT Tidak Menyala

Periksa:

```text
VCC  → 3.3V
GND  → GND
SCK  → GPIO18
MISO → GPIO19
MOSI → GPIO23
CS   → GPIO27
DC   → GPIO26
RST  → GPIO25
```

Pastikan konfigurasi `TFT_eSPI` menggunakan driver:

```text
ILI9488
```

---

## TFT Menyala tetapi Tidak Ada Tampilan

Periksa:

1. Driver ILI9488
2. SPI pin
3. CS
4. DC
5. RST
6. `TFT_eSPI/User_Setup.h`
7. Supply TFT
8. Ground ESP32 dan TFT

---

## Tombol BMS Tidak Berfungsi

Page button:

```text
GPIO32 → Button → GND
```

Firmware menggunakan:

```cpp
INPUT_PULLUP
```

Jadi:

```text
HIGH = tidak ditekan
LOW  = ditekan
```

---

## Tombol Trip Tidak Berfungsi

Trip button:

```text
GPIO33 → Button → GND
```

Konfigurasi:

```cpp
INPUT_PULLUP
```

---

## Charging Tidak Terdeteksi

Periksa:

* BMS charging flag
* CAN charger message
* Controller charging mode
* CANH/CANL
* CAN baud rate

Jika charger generic tidak mengirim CAN charger frame seperti charger original, informasi charging dapat bergantung pada status BMS/controller.

---

# ⚠️ CAN Termination

CAN Bus biasanya membutuhkan termination resistor:

```text
120Ω
```

pada kedua ujung bus.

Diagram:

```text
120Ω                         120Ω
 │                            │
CANH ───────────────────────────── CANH
CANL ───────────────────────────── CANL
 │                            │
Node                         Node
```

Pastikan modul SN65HVD230 Anda tidak memiliki termination resistor tambahan yang menyebabkan total termination tidak sesuai.

---

# 🔐 Safety

Jangan melakukan wiring atau modifikasi CAN ketika kendaraan sedang aktif tanpa memahami sistem kelistrikan kendaraan.

Untuk pengujian:

1. Matikan kendaraan.
2. Pastikan wiring benar.
3. Pastikan polaritas supply benar.
4. Periksa CANH/CANL.
5. Gunakan fuse/proteksi yang sesuai.
6. Jangan mengaktifkan CAN injection tanpa pengujian.
7. Jangan melakukan perubahan firmware ketika kendaraan sedang bergerak.

---

# 📝 License

MIT License

Copyright (c) 2026 Zekri R

---

# ❤️ Credits

Developed by:

**Zekri R / ZEKRI.ID**

Project:

**Votol BLE + WiFi + TFT Dashboard**

Platform:

**ESP32**

CAN Transceiver:

**SN65HVD230**

Display:

**ILI9488 480×320**

Target:

**Electric Vehicle / Votol Controller / BMS Monitoring**