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
