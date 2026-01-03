# 🎵 yoRadio Pilot

**Bezprzewodowy pilot do [yoRadio](https://github.com/e2002/yoradio) z wyświetlaczem OLED**  
Sterownik na ESP32-C3 Super Mini z monitorowaniem baterii i głębokim uśpieniem.

![Wersja](https://img.shields.io/badge/wersja-1.3-blue)
![Platforma](https://img.shields.io/badge/platforma-ESP32--C3-green)
![Licencja](https://img.shields.io/badge/licencja-MIT-orange)

---

## 📸 Zdjęcia



---

## ✨ Funkcje

### 🎛️ **Sterowanie**
- ⏯️ Przełączanie Play/Pauza
- ⏭️ Następna/Poprzednia stacja
- 🔊 Regulacja głośności z auto-powtarzaniem
- 💤 Automatyczne głębokie uśpienie (konfigurowalny czas)
- 🔋 Monitoring poziomu baterii z ostrzeżeniem o niskim poziomie

### 🖥️ **Wyświetlacz**
- 📊 Informacje o stacji, wykonawcy i utworze w czasie rzeczywistym
- 🔄 Automatyczne przewijanie długich tekstów
- 📶 Siła sygnału WiFi (słupki RSSI)
- 🔋 Wskaźnik procentowy baterii
- 🎵 Bitrate i format audio (MP3/AAC)
- 🌐 Wyświetlanie adresu IP

### ⚡ **Wydajność**
- 🚀 Optymalizacja przez kompilacyjne hashowanie stringów
- 🧠 Dane WebSocket chronione mutexem
- ⏱️ Throttling odświeżania ekranu (50ms)
- 💾 Statyczne parsowanie JSON (brak fragmentacji sterty)
- 🔍 Wsparcie dla polskich znaków UTF-8

---

## 🛠️ Sprzęt

### **Wymagane komponenty**
| Komponent | Model | Uwagi |
|-----------|-------|-------|
| **MCU** | ESP32-C3 Super Mini | 4MB Flash, 400KB RAM |
| **Wyświetlacz** | SSD1306 OLED 128x64 | I2C (adres 0x3C) |
| **Przyciski** | 5x mikroprzełączniki tactile | GPIO 2, 3, 4, 5, 6 |
| **Bateria** | Li-Po 3.7V | Monitorowana przez GPIO0/ADC |

### **Schemat pinów (ESP32-C3 Super Mini)**

```
┌─────────────────────┐
│  ESP32-C3 Mini      │
├─────────────────────┤
│ GPIO2  → BTN_UP     │ (Przycisk Góra)
│ GPIO3  → BTN_RIGHT  │ (Przycisk Prawo)
│ GPIO4  → BTN_CENTER │ ← Przycisk wybudzania
│ GPIO5  → BTN_LEFT   │ (Przycisk Lewo)
│ GPIO6  → BTN_DOWN   │ (Przycisk Dół)
│ GPIO0  → BAT_ADC    │ (Pomiar baterii)
│ GPIO8  → I2C_SDA    │ (OLED Data)
│ GPIO9  → I2C_SCL    │ (OLED Clock)
└─────────────────────┘
```

---

## 📦 Instalacja

### **1. Sklonuj repozytorium**
```bash
git clone https://github.com/pimowo/yoRadio_pilot.git
cd yoRadio_pilot
```

### **2. Skonfiguruj ustawienia**
Utwórz plik `src/myoptions.h` na podstawie szablonu:

```cpp
// src/myoptions.h
#ifndef MYOPTIONS_H
#define MYOPTIONS_H

// === WiFi ===
#define WIFI_SSID "Twoja_Siec_WiFi"
#define WIFI_PASS "Twoje_Haslo_WiFi"

// === Serwer yoRadio ===
#define YORADIO_IP "192.168.1.100"  // IP twojego urządzenia yoRadio

// === Wyświetlacz ===
#define OLED_BRIGHTNESS 8  // 0-15 (jasność ekranu)

// === Bateria ===
#define BATTERY_MIN_VOLTAGE 3.0   // Minimalne napięcie (rozładowana)
#define BATTERY_MAX_VOLTAGE 4.2   // Maksymalne napięcie (naładowana)
#define BATTERY_R1 100000         // Dzielnik napięcia - rezystor 1 (100kΩ)
#define BATTERY_R2 100000         // Dzielnik napięcia - rezystor 2 (100kΩ)

// === Głębokie uśpienie ===
#define DEEP_SLEEP_TIMEOUT_SEC 300        // 5 minut podczas odtwarzania
#define DEEP_SLEEP_TIMEOUT_STOPPED_SEC 60 // 1 minuta gdy zatrzymane

// === Statyczne IP (opcjonalne) ===
#define USE_STATIC_IP 0
#define STATIC_IP "192.168.1.150"
#define GATEWAY_IP "192.168.1.1"
#define SUBNET_MASK "255.255.255.0"
#define DNS1_IP "8.8.8.8"
#define DNS2_IP "8.8.4.4"

#endif
```

### **3. Kompilacja i wgranie**

#### Przy użyciu PlatformIO (zalecane):
```bash
# Wersja release (zoptymalizowana)
pio run -e release -t upload

# Wersja debug (z logowaniem przez serial)
pio run -e debug -t upload
pio device monitor
```

#### Przy użyciu Arduino IDE:
1. Zainstaluj wsparcie dla płytek ESP32: `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
2. Wybierz płytkę:  **ESP32C3 Dev Module**
3. Zainstaluj biblioteki (patrz `platformio.ini` → `lib_deps`)
4. Wgraj kod! 

---

## 🎮 Obsługa

### **Sterowanie przyciskami**

```
     [GÓRA]   → Głośność +
     [DÓŁ]    → Głośność -
   [CENTER]   → Przełącz Play/Pauza
    [LEWO]    → Poprzednia Stacja
    [PRAWO]   → Następna Stacja
```

**Długie przytrzymanie CENTER** (gdy urządzenie śpi) → Wybudzenie

### **Ekrany wyświetlacza**

#### **Ekran główny**
```
┌────────────────────────────┐
│ ★ Nazwa Stacji ★           │ ← Przewijanie
├────────────────────────────┤
│ Wykonawca                  │ ← Przewijanie
│ Tytuł Utworu               │ ← Przewijanie
├────────────────────────────┤
│ 📶 │ 🔋 75% │ 🔊 42 │ 320 MP3 │
└────────────────────────────┘
```

#### **Ekran głośności** (tymczasowy, 2 sekundy)
```
┌────────────────────────────┐
│      GŁOŚNOŚĆ              │
├────────────────────────────┤
│          42                │
├────────────────────────────┤
│ IP: 192.168.1.150          │
└────────────────────────────┘
```

---

## 🏗️ Struktura projektu

```
yoRadio_pilot/
├── src/
│   ├── main.cpp          # Główna pętla aplikacji
│   ├── config.h          # Stałe sprzętowe i wyświetlacza
│   ├── battery.h/. cpp    # Moduł monitorowania baterii
│   ├── myoptions.h       # Konfiguracja użytkownika (w . gitignore)
│   └── font5x7.h         # Niestandardowa czcionka z polskimi znakami
├── platformio.ini        # Konfiguracja kompilacji
└── README.md             # Ten plik
```

---

## 🔧 Konfiguracja

### **Profile kompilacji**
| Środowisko | Optymalizacja | Debug | Przeznaczenie |
|------------|---------------|-------|---------------|
| `release` | `-Os` (rozmiar) | ❌ Wyłączony | Produkcja |
| `debug` | `-O0` (brak) | ✅ Serial | Rozwój |

### **Optymalizacja pamięci**
- Statyczny bufor JSON (1KB, brak fragmentacji)
- Stringi w PROGMEM (oszczędność RAM)
- Hashowanie w czasie kompilacji
- Skracanie ścieżek dla długich ścieżek frameworka

---

## 📡 Integracja z yoRadio

### **Protokół WebSocket**
Połączenie z `ws://YORADIO_IP: 80/ws`

**Wysyłane komendy:**
```javascript
getindex=1    // Żądanie początkowych danych
toggle=1      // Play/Pauza
next=1        // Następna stacja
prev=1        // Poprzednia stacja
volp=1        // Głośność +1
volm=1        // Głośność -1
```

**Odbierane dane:**
```json
{
  "payload": [
    {"id": "nameset", "value": "Nazwa Stacji"},
    {"id": "meta", "value": "Wykonawca - Utwór"},
    {"id": "volume", "value": 42},
    {"id": "bitrate", "value": 320},
    {"id": "fmt", "value": "mp3"},
    {"id": "playerwrap", "value": "playing"}
  ]
}
```

---

## 🐛 Rozwiązywanie problemów

### **Wyświetlacz nie działa**
- Sprawdź adres I2C:  `display.begin(SSD1306_SWITCHCAPVCC, 0x3C)`
- Niektóre wyświetlacze używają `0x3D` zamiast `0x3C`
- Zweryfikuj okablowanie SDA/SCL

### **WiFi nie łączy się**
1. Sprawdź `WIFI_SSID` i `WIFI_PASS` w `myoptions.h`
2. Włącz tryb debug:  `pio run -e debug -t upload && pio device monitor`
3. Spróbuj wyłączyć statyczne IP: `#define USE_STATIC_IP 0`

### **Timeout WebSocket**
- Zweryfikuj adres IP yoRadio
- Sprawdź firewall na urządzeniu yoRadio
- Upewnij się, że WebSocket jest włączony w yoRadio (port 80)

### **Nieprawidłowy procent baterii**
- Zmierz rzeczywiste napięcie baterii
- Dostosuj `BATTERY_MIN_VOLTAGE` / `BATTERY_MAX_VOLTAGE`
- Jeśli używasz dzielnika napięcia, sprawdź `BATTERY_R1` / `BATTERY_R2`

### **Problemy z głębokim uśpieniem (ESP32-C3)**
- **GPIO4 (BTN_CENTER)** jest na sztywno ustawiony jako pin wybudzający
- Tylko **GPIO0-5** obsługują wybudzanie z głębokiego snu na ESP32-C3
- Poziom wybudzania:  **LOW** (wciśnięty przycisk = GND)

---

## 📄 Licencja

Licencja MIT - zobacz plik [LICENSE](LICENSE)

---

## 🤝 Współpraca

1. Zforkuj repozytorium
2. Utwórz branch funkcji:  `git checkout -b feature/wspaniala-funkcja`
3. Commit: `git commit -m 'Dodano wspaniałą funkcję'`
4. Push: `git push origin feature/wspaniala-funkcja`
5. Otwórz Pull Request

---

## 📞 Wsparcie

- **Problemy**:  [GitHub Issues](https://github.com/pimowo/yoRadio_pilot/issues)
- **yoRadio**: [e2002/yoradio](https://github.com/e2002/yoradio)

---

## 🙏 Podziękowania

- [yoRadio](https://github.com/e2002/yoradio) autorstwa **e2002** - wspaniały projekt radia internetowego
- Biblioteka [WebSockets](https://github.com/Links2004/arduinoWebSockets) autorstwa **Links2004**
- Biblioteka [Adafruit SSD1306](https://github.com/adafruit/Adafruit_SSD1306)

---

## 📊 Statystyki

![Rozmiar kodu](https://img.shields.io/github/languages/code-size/pimowo/yoRadio_pilot)
![Ostatni commit](https://img.shields.io/github/last-commit/pimowo/yoRadio_pilot)

---

**Stworzone z ❤️ dla społeczności yoRadio**