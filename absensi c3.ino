/*
 * =================================================================================
 * ABSENSI SEKOLAH - ESP32-C3 SUPER MINI (USB-C) + RFID RC522 + 2 LED + BUZZER AKTIF
 * Tanpa LCD. Protokol server sama dengan program lama (ESP32 DevKit V1).
 * =================================================================================
 * PIN:
 *  RC522 : SS(SDA) -> GPIO5, SCK -> GPIO6, MISO -> GPIO7, MOSI -> GPIO10,
 *          RST -> GPIO4, VCC -> 3.3V (WAJIB 3.3V), GND -> GND
 *  LED hijau -> resistor -> GPIO0 -> GND
 *  LED merah -> resistor -> GPIO1 -> GND
 *  Buzzer aktif (+) -> GPIO3, (-) -> GND
 *
 * Pin strapping ESP32-C3 adalah GPIO2, GPIO8, GPIO9 (tidak dipakai di sini).
 * GPIO20/21 = UART, GPIO8 = LED onboard, GPIO9 = tombol BOOT (tidak dipakai).
 *
 * POLA INDIKATOR:
 *  Berhasil   : LED hijau 0,4 detik + 1x bip pendek (150 ms)
 *  Peringatan : kedua LED kedip 2x + 2x bip pendek (COOLDOWN / ALREADY_COMPLETED)
 *  Gagal      : LED merah 0,7 detik + 1x bip panjang (400 ms)
 *  Offline    : LED merah MENYALA TERUS selama Wi-Fi belum tersambung
 *  Online     : LED hijau kedip + 3x bip pendek, lalu merah padam
 *  Semua indikator dimatikan setelah selesai.
 * =================================================================================
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <SPI.h>
#include <MFRC522.h>
#include <ArduinoJson.h>

// ==========================================
// 1. PENGATURAN WIFI & SERVER (SAMA DENGAN PROGRAM LAMA)
// ==========================================
const char* WIFI_SSID     = "Sumbul";
const char* WIFI_PASSWORD = "Sumbulganteng";

const char* HOSTINGER_API_URL = "https://absensi.smkmu04medan.sch.id/api.php?action=tap_nfc";
const char* JAM_BATAS         = "07:15:00";

// Set ke 1 hanya jika Wi-Fi Super Mini Anda sering putus / sulit konek
// (beberapa board Super Mini butuh daya pancar Wi-Fi diturunkan).
#define WIFI_TX_POWER_FIX 1

// ==========================================
// 2. PENETAPAN PIN
// ==========================================
// Catatan: nama SS/SCK/MISO/MOSI sudah dipakai oleh core ESP32, jadi pakai awalan RC522_
#define RC522_RST_PIN    4
#define RC522_SS_PIN     5
#define RC522_SCK_PIN    6
#define RC522_MISO_PIN   7
#define RC522_MOSI_PIN  10

#define PIN_LED_HIJAU    0
#define PIN_LED_MERAH    1
#define PIN_BUZZER       3

MFRC522 mfrc522(RC522_SS_PIN, RC522_RST_PIN);

// ==========================================
// 3. PENGATURAN WAKTU
// ==========================================
const unsigned long DEBOUNCE_DELAY      = 2000;   // UID sama diabaikan 2 detik sejak tap
const unsigned long WIFI_RETRY_CEPAT     = 1500;  // jeda coba ulang 3x pertama setelah gagal
const unsigned long WIFI_RETRY_LAMBAT    = 8000;  // jeda coba ulang berikutnya (hemat daya/panas)
const unsigned long WIFI_ATTEMPT_MAX     = 15000; // percobaan tanpa hasil dianggap menggantung
const unsigned long RFID_RETRY_INTERVAL  = 2000;

String lastUidScanned = "";
unsigned long lastScanTime = 0;
unsigned long lastWifiAttempt = 0;
bool wifiSebelumnya = false;
volatile bool percobaanGagal = false;  // diisi oleh event Wi-Fi saat satu percobaan berakhir gagal
int wifiGagalBeruntun = 0;
bool rfidReady = false;

// ==========================================
// 4. UTILITAS LED & BUZZER
// ==========================================
void semuaOff() {
  digitalWrite(PIN_LED_HIJAU, LOW);
  digitalWrite(PIN_LED_MERAH, LOW);
  digitalWrite(PIN_BUZZER, LOW);
}

void bip(unsigned int ms) {
  digitalWrite(PIN_BUZZER, HIGH);
  delay(ms);
  digitalWrite(PIN_BUZZER, LOW);
}

// ==========================================
// 5. FEEDBACK INDIKATOR (LED & BUZZER)
// ==========================================
void feedbackSukses(const String& jam, const String& nama, const String& tipe) {
  Serial.printf(">>> SUKSES: %s | %s | %s <<<\n", tipe.c_str(), nama.c_str(), jam.c_str());

  digitalWrite(PIN_LED_HIJAU, HIGH);
  bip(150);          // bip pendek
  delay(250);
  semuaOff();
}

void feedbackPeringatan(const String& pesan) {
  Serial.println("[PERINGATAN] " + pesan);

  for (int i = 0; i < 2; i++) {
    digitalWrite(PIN_LED_HIJAU, HIGH);
    digitalWrite(PIN_LED_MERAH, HIGH);
    digitalWrite(PIN_BUZZER, HIGH);
    delay(100);
    semuaOff();
    delay(100);
  }
  semuaOff();
}

void feedbackGagal(const String& pesan) {
  Serial.println("[GAGAL] " + pesan);

  digitalWrite(PIN_LED_MERAH, HIGH);
  bip(400);          // bip panjang
  delay(300);
  semuaOff();
}

// ==========================================
// 6. KONEKSI WIFI (NON-BLOKIR, SAMBUNG ULANG CEPAT)
// ==========================================
void tandaOnline() {
  // 3x bip pendek + LED hijau tanda alat online dan siap
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED_HIJAU, HIGH);
    digitalWrite(PIN_BUZZER, HIGH);
    delay(70);
    semuaOff();
    delay(70);
  }
}

// Dipanggil otomatis saat satu percobaan menyambung berakhir gagal / putus.
// Kode alasan: 201 = AP tidak ditemukan, 15/202/204 = password/autentikasi, 205 = koneksi gagal
void onWiFiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
    Serial.printf("[WiFi] Putus/gagal, kode alasan: %d\n", info.wifi_sta_disconnected.reason);
    percobaanGagal = true;
  }
}

// Diagnosa (sekali saja, setelah 3x gagal): tampilkan jaringan yang terlihat
void diagnosaWiFi() {
  int n = WiFi.scanNetworks();
  Serial.printf("[WiFi] Terdeteksi %d jaringan 2.4GHz:\n", n);
  bool ketemu = false;
  for (int i = 0; i < n; i++) {
    Serial.printf("   %s (%d dBm)\n", WiFi.SSID(i).c_str(), (int)WiFi.RSSI(i));
    if (WiFi.SSID(i) == String(WIFI_SSID)) ketemu = true;
  }
  if (ketemu) {
    Serial.println("[WiFi] SSID DITEMUKAN -> kemungkinan password salah atau sinyal lemah");
  } else {
    Serial.println("[WiFi] SSID TIDAK DITEMUKAN -> cek nama Wi-Fi, jarak, dan pastikan 2.4GHz");
  }
  WiFi.scanDelete();
}

// Memulai satu percobaan menyambung TANPA menunggu (tidak memblokir).
void mulaiPercobaanWiFi(bool bersih) {
  lastWifiAttempt = millis();
  if (bersih) {
    WiFi.disconnect(true);
    delay(200);
  }
  WiFi.mode(WIFI_STA);
#if WIFI_TX_POWER_FIX
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
#if WIFI_TX_POWER_FIX
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // diulang setelah begin(): beberapa board Super Mini butuh ini
#endif
  percobaanGagal = false;
}

// Dipanggil tiap putaran loop(): TIDAK memblokir, jadi tap kartu tetap responsif.
void cekStatusWiFi() {
  bool terhubung = (WiFi.status() == WL_CONNECTED);

  if (terhubung && !wifiSebelumnya) {
    wifiGagalBeruntun = 0;
    Serial.print("[WiFi] Terhubung! IP: ");
    Serial.println(WiFi.localIP());
    tandaOnline();   // berakhir dengan semua LED mati (merah padam)
  } else if (!terhubung && wifiSebelumnya) {
    Serial.println("[WiFi] Terputus");
  }
  wifiSebelumnya = terhubung;

  if (!terhubung) {
    unsigned long sejak = millis() - lastWifiAttempt;
    unsigned long jeda = (wifiGagalBeruntun < 3) ? WIFI_RETRY_CEPAT : WIFI_RETRY_LAMBAT;

    if (percobaanGagal && sejak >= jeda) {
      // Percobaan sebelumnya sudah berakhir -> coba lagi segera (aman, tidak bentrok)
      wifiGagalBeruntun++;
      if (wifiGagalBeruntun == 3) diagnosaWiFi();
      Serial.println("[WiFi] Mencoba sambung ulang...");
      mulaiPercobaanWiFi(false);
    } else if (!percobaanGagal && sejak >= WIFI_ATTEMPT_MAX) {
      // Percobaan menggantung tanpa hasil -> reset bersih
      wifiGagalBeruntun++;
      Serial.println("[WiFi] Percobaan menggantung, reset Wi-Fi...");
      mulaiPercobaanWiFi(true);
    }

    // LED merah MENYALA TERUS selama belum tersambung
    digitalWrite(PIN_LED_MERAH, HIGH);
  }
}

// ==========================================
// 7. KIRIM DATA KE SERVER VIA HTTPS
// ==========================================
void kirimAbsensiKeServer(const String& uid) {
  // Tidak menunggu sambung ulang (itu membuat alat macet): langsung gagal cepat.
  if (WiFi.status() != WL_CONNECTED) {
    feedbackGagal("WiFi terputus, gagal absen (sambung ulang berjalan otomatis)");
    return;
  }

  Serial.println("\n-------------------------------------------");
  Serial.print("[NFC] UID Kartu: ");
  Serial.println(uid);

  WiFiClientSecure client;
  client.setInsecure();  // sama seperti program lama

  HTTPClient https;
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  https.setUserAgent("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");

  if (!https.begin(client, HOSTINGER_API_URL)) {
    feedbackGagal("URL error, cek link web");
    return;
  }

  https.addHeader("Content-Type", "application/json");
  https.setConnectTimeout(8000);
  https.setTimeout(8000);

  // Format JSON sama persis dengan program lama
  String jsonPayload = "{\"action\":\"tap_nfc\",\"uid\":\"" + uid + "\",\"jam_batas\":\"" + String(JAM_BATAS) + "\"}";
  int httpCode = https.POST(jsonPayload);

  if (httpCode <= 0) {
    Serial.printf("[HTTP ERROR] %s\n", https.errorToString(httpCode).c_str());
    https.end();
    feedbackGagal("Server timeout / gagal menghubungi web");
    return;
  }

  Serial.printf("[HTTP Status] Code: %d\n", httpCode);
  String responseBody = https.getString();
  https.end();
  Serial.println("[HTTP Response] " + responseBody);

  if (httpCode != 200) {
    Serial.printf("[ERROR] Web merespons dengan HTTP %d\n", httpCode);
    feedbackGagal("HTTP " + String(httpCode) + ", cek web");
    return;
  }

  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, responseBody);

  if (error) {
    Serial.print("[ERROR PARSE JSON] ");
    Serial.println(error.c_str());
    String cuplikan = responseBody;
    cuplikan.trim();
    if (cuplikan.length() > 80) cuplikan = cuplikan.substring(0, 80);
    feedbackGagal("Bukan JSON valid: " + (cuplikan.length() > 0 ? cuplikan : String("respon kosong")));
    return;
  }

  bool success   = doc["success"] | false;
  String status  = doc["status"]  | "";
  String message = doc["message"] | "";

  if (success) {
    String nama = doc["siswa"]["nama"] | "Siswa";
    String tipe = doc["tipe"]          | "Absen";
    String jam  = doc["jam"]           | "";
    feedbackSukses(jam, nama, tipe);
  } else {
    Serial.println("[GAGAL] " + message);
    if (status == "NOT_FOUND") {
      Serial.println(">> Kartu belum terdaftar. Daftarkan UID ini lewat website: " + uid);
      feedbackGagal("Kartu belum terdaftar: " + uid);
    } else if (status == "COOLDOWN") {
      feedbackPeringatan("Sudah absen, tunggu 2 menit");
    } else if (status == "ALREADY_COMPLETED") {
      feedbackPeringatan("Absen masuk & pulang sudah lengkap");
    } else {
      feedbackGagal("Gagal absen: " + message);
    }
  }
}

// ==========================================
// 8. INISIALISASI RC522
// ==========================================
bool inisialisasiRfid() {
  mfrc522.PCD_Init();
  delay(50);
  byte v = mfrc522.PCD_ReadRegister(mfrc522.VersionReg);
  if (v == 0x00 || v == 0xFF) {
    Serial.println("[ERROR] Sensor RC522 tidak terdeteksi! Periksa kabel SPI.");
    return false;
  }
  Serial.printf("[RC522] Terdeteksi, versi firmware 0x%02X\n", v);
  return true;
}

// ==========================================
// 9. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n===========================================");
  Serial.println("  ESP32-C3 SUPER MINI - ABSENSI RFID (LED+BUZZER)");
  Serial.println("===========================================");

  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED_HIJAU, OUTPUT);
  pinMode(PIN_LED_MERAH, OUTPUT);
  semuaOff();

  // Mulai Wi-Fi LEBIH DULU (tanpa menunggu) supaya menyambung sambil RC522 disiapkan
  WiFi.persistent(false);          // jangan tulis kredensial ke flash tiap konek
  WiFi.onEvent(onWiFiEvent);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);    // sambung ulang diatur manual agar cepat tapi tidak membuat panas
  Serial.print("Menghubungkan ke WiFi: ");
  Serial.println(WIFI_SSID);
  mulaiPercobaanWiFi(false);
  digitalWrite(PIN_LED_MERAH, HIGH);  // merah menyala terus sampai tersambung

  // SPI dengan pin eksplisit: SPI.begin(SCK, MISO, MOSI, SS)
  SPI.begin(RC522_SCK_PIN, RC522_MISO_PIN, RC522_MOSI_PIN, RC522_SS_PIN);
  rfidReady = inisialisasiRfid();
}

// ==========================================
// 10. LOOP
// ==========================================
void loop() {
  // Pantau Wi-Fi: sambung ulang cepat tanpa memblokir + LED merah menyala terus saat offline
  cekStatusWiFi();

  // RC522 tidak terdeteksi: kedip merah, coba lagi berkala (tidak menggantung selamanya)
  if (!rfidReady) {
    digitalWrite(PIN_LED_MERAH, HIGH);
    delay(300);
    digitalWrite(PIN_LED_MERAH, LOW);
    delay(RFID_RETRY_INTERVAL - 300);
    rfidReady = inisialisasiRfid();
    return;
  }

  // 1. Cek keberadaan kartu baru
  if (!mfrc522.PICC_IsNewCardPresent()) {
    delay(50);
    return;
  }

  // 2. Baca serial UID kartu
  if (!mfrc522.PICC_ReadCardSerial()) {
    delay(50);
    return;
  }

  // 3. Format UID sama seperti program lama (contoh: "04:A2:3B:5C")
  String uidStr = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (i > 0) uidStr += ":";
    if (mfrc522.uid.uidByte[i] < 0x10) uidStr += "0";
    uidStr += String(mfrc522.uid.uidByte[i], HEX);
  }
  uidStr.toUpperCase();

  // 4. Debounce: UID sama dalam 2 detik sejak tap diabaikan
  unsigned long now = millis();
  if (uidStr == lastUidScanned && (now - lastScanTime < DEBOUNCE_DELAY)) {
    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    return;
  }

  lastUidScanned = uidStr;
  lastScanTime = now;

  // 5. Kirim ke server
  kirimAbsensiKeServer(uidStr);

  semuaOff();

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
}
