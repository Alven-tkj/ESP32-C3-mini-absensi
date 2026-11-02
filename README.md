# Absensi Sekolah Berbasis RFID

Sistem absensi sekolah menggunakan ESP32-C3 Super Mini, RFID RC522, dua LED indikator, dan buzzer aktif. Sistem ini tidak menggunakan LCD dan tetap menggunakan protokol server dari program sebelumnya.

## Komponen dan Pin

### RFID RC522
| Pin RFID | Pin ESP32-C3 |
|---|---|
| SDA | GPIO 5 |
| SCK | GPIO 6 |
| MISO | GPIO 7 |
| MOSI | GPIO 10 |
| RST | GPIO 4 |
| VCC | 3.3V |
| GND | GND |

### LED dan Buzzer
| Komponen | Pin ESP32-C3 |
|---|---|
| LED Hijau | GPIO 0 |
| LED Merah | GPIO 1 |
| Buzzer Aktif (+) | GPIO 3 |
| Buzzer Aktif (-) | GND |

## Indikator Sistem

| Kondisi | Indikator |
|---|---|
| Berhasil | LED hijau menyala 0,4 detik dan buzzer berbunyi 1 kali |
| Peringatan | Kedua LED berkedip 2 kali dan buzzer berbunyi 2 kali |
| Gagal | LED merah menyala 0,7 detik dan buzzer berbunyi panjang |
| Offline | LED merah menyala selama Wi-Fi belum terhubung |
| Online | LED hijau berkedip dan buzzer berbunyi 3 kali |

Setelah setiap proses indikator selesai, LED dan buzzer akan kembali mati.

## Catatan

- Gunakan tegangan 3.3V untuk RFID RC522.
- Pasang resistor pada masing-masing LED.
- Pastikan semua komponen terhubung dengan benar.
- Sistem menggunakan ESP32-C3 Super Mini dengan konektor USB Type-C.
- 
