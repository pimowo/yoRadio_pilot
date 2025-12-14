# yoRadio_pilot v0.3

Pilot zdalnego sterowania dla yoRadio - kompaktowy kontroler internetowego radia bazujący na ESP32-S3 Super Mini z wyświetlaczem OLED SSD1306.

## 📋 Spis treści

- [Funkcje](#-funkcje)
- [Specyfikacja sprzętowa](#-specyfikacja-sprzętowa)
- [Konfiguracja pinów](#-konfiguracja-pinów)
- [Wymagania](#-wymagania)
- [Instalacja](#-instalacja)
- [Konfiguracja](#-konfiguracja)
- [Obsługa](#-obsługa)
- [Funkcje zaawansowane](#-funkcje-zaawansowane)
- [Aktualizacje OTA](#-aktualizacje-ota)
- [Rozwiązywanie problemów](#-rozwiązywanie-problemów)
- [Struktura projektu](#-struktura-projektu)

## 🎯 Funkcje

### Podstawowe
- **Sterowanie radiem internetowym yoRadio** przez WebSocket
- **Wyświetlacz OLED 128x64** z dynamicznym trybem 2/3 linii
- **Obsługa wielu rádií** - przełączanie między różnymi instancjami yoRadio
- **Enkoder obrotowy** do regulacji głośności i nawigacji
- **5 przycisków sterujących**: PLAY, PREV, NEXT, CENTER, VOLUME
- **Przewijanie sekwencyjne** - płynne przewijanie długich tekstów
- **Wyświetlanie bitrate** podczas odtwarzania
- **Wskaźnik RSSI** (gdy NUM_RADIOS = 1)
- **Numer radia** w lewym dolnym rogu (gdy NUM_RADIOS > 1)

### Zaawansowane
- **Deep Sleep** - tryb uśpienia po okresie bezczynności (domyślnie 5 min)
- **Monitorowanie baterii** z filtrowaniem ADC (średnia krocząca)
- **Watchdog Timer** - automatyczny restart przy zawieszeniu (120s)
- **Detekcja timeout WebSocket** - automatyczne ponowne połączenie
- **Aktualizacje OTA** - bezprzewodowa aktualizacja firmware
- **Polskie znaki UTF-8** - pełna obsługa ą, ć, ę, ł, ń, ó, ś, ź, ż
- **Długie naciśnięcie CENTER** (>1s) - przełączanie między radiami
- **Ekran głośności** - wyświetlanie poziomu głośności przy regulacji

## 🔧 Specyfikacja sprzętowa

### Platforma
- **Mikrokontroler**: ESP32-S3 Super Mini
  - Dual-core Xtensa LX7 do 240 MHz
  - 512 KB SRAM, 384 KB ROM
  - 8 MB Flash (wbudowany)
  - WiFi 802.11 b/g/n (2.4 GHz)
  - Bluetooth 5.0 LE
  - USB-C native (bez konwertera USB-UART)
  - Wymiary: 22.5mm × 18mm

### Wyświetlacz
- **Model**: SSD1306 OLED 128x64 pikseli
- **Interfejs**: I2C (adres 0x3C)
- **Kolor**: Monochromatyczny (biały/niebieski)

### Elementy sterujące
- **Enkoder obrotowy** z przyciskiem (EC11 lub podobny)
- **4 przyciski taktowe** (PLAY, PREV, NEXT, VOLUME)

### Zasilanie
- **Napięcie**: 3.3V - 5V przez USB-C lub pin 5V
- **Pomiar baterii**: przez dzielnik napięcia na GPIO4
- **Pobór prądu**:
  - Aktywny: ~80-120 mA
  - Deep Sleep: <1 mA

## 📌 Konfiguracja pinów

### Wyświetlacz I2C
```cpp
SDA: GPIO8
SCL: GPIO9
```

### Enkoder obrotowy
```cpp
CLK (A): GPIO1
DT (B):  GPIO2
SW:      GPIO3
```

### Przyciski
```cpp
PLAY:    GPIO5
PREV:    GPIO6
NEXT:    GPIO7
CENTER:  GPIO10
VOLUME:  GPIO11
```

### Monitorowanie baterii
```cpp
ADC:     GPIO4 (dzielnik napięcia)
```

### Schemat podłączenia dzielnika napięcia baterii
```
VBAT ---[10kΩ]---+---[10kΩ]--- GND
                  |
                GPIO4
```

## 📦 Wymagania

### Biblioteki Arduino
```cpp
WiFi.h              // Wbudowana w ESP32
WebSocketsClient.h  // https://github.com/Links2004/arduinoWebSockets
ArduinoJson.h       // https://arduinojson.org/
Wire.h              // Wbudowana
Adafruit_GFX.h      // https://github.com/adafruit/Adafruit-GFX-Library
Adafruit_SSD1306.h  // https://github.com/adafruit/Adafruit_SSD1306
ArduinoOTA.h        // Wbudowana w ESP32
esp_task_wdt.h      // Wbudowana w ESP32
```

### Instalacja bibliotek przez Arduino IDE
1. Otwórz Arduino IDE
2. Przejdź do **Sketch → Include Library → Manage Libraries**
3. Wyszukaj i zainstaluj:
   - `WebSockets` by Markus Sattler
   - `ArduinoJson` by Benoit Blanchon
   - `Adafruit GFX Library`
   - `Adafruit SSD1306`

### Konfiguracja Arduino IDE dla ESP32-S3
1. Dodaj URL do Board Manager: `https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json`
2. Zainstaluj **ESP32 by Espressif Systems**
3. Wybierz płytkę: **ESP32S3 Dev Module**
4. Ustawienia partycji: **Default 4MB with spiffs**

## 💾 Instalacja

### 1. Pobranie projektu
```bash
git clone https://github.com/pimowo/yoRadio_pilot.git
cd yoRadio_pilot
```

### 2. Otwarcie w Arduino IDE
- Otwórz plik `yoRadio_pilot.ino` w Arduino IDE

### 3. Konfiguracja połączenia
Edytuj sekcję konfiguracyjną w pliku `.ino`:

```cpp
// ============================================
// KONFIGURACJA - EDYTUJ PRZED WGRANIEM
// ============================================

// WiFi
const char* ssid = "TWOJA_SIEC_WIFI";
const char* password = "TWOJE_HASLO_WIFI";

// Definicja rádií (adres IP i port)
const char* radioHosts[] = {
  "192.168.1.100",  // Radio 1
  "192.168.1.101"   // Radio 2 (opcjonalnie)
};
const int radioPorts[] = {
  8101,  // Port WebSocket Radio 1
  8101   // Port WebSocket Radio 2
};
const int NUM_RADIOS = 2;  // Liczba rádií (1 lub więcej)

// Deep Sleep
const unsigned long INACTIVITY_TIMEOUT = 300000; // 5 minut (ms)

// Bateria
const float BATTERY_LOW_VOLTAGE = 3.3;  // Próg niskiego napięcia
const int ADC_SAMPLES = 10;              // Liczba próbek do uśredniania
```

### 4. Wgranie firmware
1. Podłącz ESP32-S3 Super Mini przez USB-C
2. Wybierz właściwy port COM w Arduino IDE
3. Kliknij **Upload**
4. Poczekaj na zakończenie kompilacji i wgrania

## ⚙️ Konfiguracja

### Parametry yoRadio
Upewnij się, że Twoja instancja yoRadio ma włączony WebSocket:
- Domyślny port WebSocket: **8101**
- Sprawdź w panelu konfiguracyjnym yoRadio: **Settings → Network**

### Tryb wyświetlania
Pilot automatycznie przełącza się między trybami:
- **3 linie**: Gdy tekst zmieści się w 3 liniach po 21 znaków
- **2 linie**: Gdy tekst jest dłuższy (większa czcionka, lepsze przewijanie)

### Konfiguracja wielu rádií
Aby używać wielu instancji yoRadio:
1. Ustaw `NUM_RADIOS` na liczbę swoich rádií
2. Podaj adresy IP w tablicy `radioHosts[]`
3. Podaj porty w tablicy `radioPorts[]`
4. Długie naciśnięcie **CENTER** (>1s) przełącza między radiami

Przykład dla 3 rádií:
```cpp
const char* radioHosts[] = {
  "192.168.1.100",
  "192.168.1.101",
  "192.168.1.102"
};
const int radioPorts[] = {8101, 8101, 8101};
const int NUM_RADIOS = 3;
```

### Deep Sleep
Domyślnie pilot przechodzi w tryb uśpienia po 5 minutach bezczynności:
```cpp
const unsigned long INACTIVITY_TIMEOUT = 300000; // ms
```

Wyłączenie Deep Sleep:
```cpp
const unsigned long INACTIVITY_TIMEOUT = 0; // wyłącz
```

Budzenie z Deep Sleep:
- Naciśnięcie **dowolnego przycisku** lub obrócenie **enkodera**

## 🎮 Obsługa

### Przyciski

| Przycisk | Funkcja | Długie naciśnięcie (>1s) |
|----------|---------|--------------------------|
| **PLAY** | Play/Pause/Stop | - |
| **PREV** | Poprzednia stacja | - |
| **NEXT** | Następna stacja | - |
| **CENTER** | Potwierdzenie / Wyjście z menu | **Przełączanie między radiami** |
| **VOLUME** | Wejście do ekranu głośności | - |

### Enkoder obrotowy
- **Obrót**: Regulacja głośności (w każdym ekranie)
- **Naciśnięcie**: Funkcja CENTER

### Ekrany

#### Ekran główny
```
┌─────────────────────────┐
│ ♫ Nazwa stacji         ↻│
│ Tytuł utworu...         │
│ Artysta...              │
│ 1  [128k]         85% ● │
└─────────────────────────┘
```
- **Linia 1**: Ikona statusu + nazwa stacji + wskaźnik połączenia
- **Linia 2-3**: Tytuł i artysta (przewijanie sekwencyjne)
- **Linia 4**: Numer radia (jeśli >1) + bitrate + poziom baterii + wskaźnik WiFi

#### Ekran głośności (po naciśnięciu VOLUME)
```
┌─────────────────────────┐
│                         │
│      GŁOŚNOŚĆ: 75%      │
│   ███████████████░░░    │
│                         │
└─────────────────────────┘
```
- Wyświetla się przez 3 sekundy
- Regulacja enkoderem
- Automatyczny powrót do ekranu głównego

### Wskaźniki statusu

| Ikona | Znaczenie |
|-------|-----------|
| ♫ | Odtwarzanie |
| ❚❚ | Pauza |
| ■ | Stop |
| ↻ | Łączenie z WebSocket |
| ✓ | Połączony |
| ✗ | Rozłączony |
| ● | WiFi połączone |
| ○ | WiFi rozłączone |

### Wskaźnik baterii
```
100-80%: ████
79-60%:  ███░
59-40%:  ██░░
39-20%:  █░░░
<20%:    ░░░░ (ostrzeżenie)
```

## 🚀 Funkcje zaawansowane

### Przewijanie sekwencyjne
- Długie teksty przewijają się automatycznie
- Najpierw przewija się linia 2 (tytuł)
- Po zakończeniu przewija się linia 3 (artysta)
- Sekwencja powtarza się w pętli
- Wygładzone przewijanie co 300ms

### Filtrowanie ADC baterii
- Średnia krocząca z 10 próbek
- Eliminuje szumy pomiarowe
- Aktualizacja co 5 sekund
- Współczynnik korekcyjny: 2.0 (dla dzielnika 1:1)

### Watchdog Timer
- Timeout: 120 sekund
- Automatyczny restart przy zawieszeniu
- Okresowe resetowanie w głównej pętli
- Zabezpiecza przed zawieszeniem programu

### Detekcja timeout WebSocket
- Sprawdzanie co 5 sekund
- Timeout: brak komunikacji przez >15 sekund
- Automatyczne ponowne połączenie
- Wyświetlanie informacji o rozłączeniu

### Obsługa wielu rádií
- Przechowywanie stanu każdego radia
- Przełączanie długim naciśnięciem CENTER (>1s)
- Wyświetlanie numeru aktywnego radia (1, 2, 3...)
- Automatyczne połączenie z wybranym radiem

### Polskie znaki UTF-8
Pełna obsługa polskich znaków diakrytycznych:
- ą, ć, ę, ł, ń, ó, ś, ź, ż
- Automatyczna konwersja UTF-8 w czasie rzeczywistym
- Renderowanie na wyświetlaczu OLED

## 🔄 Aktualizacje OTA

### Aktywacja OTA
1. Upewnij się, że pilot i komputer są w tej samej sieci
2. W Arduino IDE:
   - **Tools → Port → Network Ports**
   - Wybierz `yoRadio_pilot at [IP]`
3. Wgraj nowy firmware standardową metodą

### Konfiguracja OTA
```cpp
ArduinoOTA.setHostname("yoRadio_pilot");
ArduinoOTA.setPassword("admin");  // Opcjonalnie
```

### Zabezpieczenia OTA
- Domyślnie bez hasła (można dodać w kodzie)
- Działa tylko w tej samej sieci lokalnej
- Automatyczne restarty po aktualizacji

## 🔍 Rozwiązywanie problemów

### Pilot nie łączy się z WiFi
**Objawy**: Komunikat "Laczenie WiFi..." nie znika
**Rozwiązania**:
- Sprawdź poprawność SSID i hasła
- Upewnij się, że sieć WiFi jest dostępna (2.4 GHz)
- Sprawdź siłę sygnału WiFi
- Zrestartuj router WiFi

### Brak połączenia WebSocket
**Objawy**: Wyświetlacz pokazuje "Laczenie..." lub "Rozlaczono"
**Rozwiązania**:
- Sprawdź, czy yoRadio działa i jest dostępne przez przeglądarkę
- Zweryfikuj adres IP i port w konfiguracji
- Upewnij się, że WebSocket jest włączony w yoRadio
- Sprawdź firewall/router - czy przepuszcza port 8101
- Sprawdź logi yoRadio pod kątem błędów WebSocket

### Wyświetlacz nie pokazuje tekstu
**Objawy**: Czarny ekran lub tylko ramki
**Rozwiązania**:
- Sprawdź połączenia I2C (SDA: GPIO8, SCL: GPIO9)
- Zweryfikuj adres I2C wyświetlacza (domyślnie 0x3C)
- Sprawdź zasilanie wyświetlacza (3.3V)
- Użyj skanera I2C do wykrycia adresu

### Enkoder nie działa poprawnie
**Objawy**: Głośność zmienia się chaotycznie lub wcale
**Rozwiązania**:
- Sprawdź połączenia enkodera (CLK: GPIO1, DT: GPIO2)
- Dodaj kondensatory 100nF między pinami a GND (debouncing)
- Sprawdź stan mechaniczny enkodera (zużycie styków)
- Wymień enkoder na nowy

### Przyciski nie reagują
**Objawy**: Brak reakcji na naciśnięcia przycisków
**Rozwiązania**:
- Sprawdź połączenia przycisków z właściwymi GPIO
- Zweryfikuj typ przycisków (normalnie otwarte)
- Sprawdź pull-up resistory (wbudowane w ESP32)
- Użyj multimetru do sprawdzenia ciągłości

### Pilot się zawiesza
**Objawy**: Brak reakcji, wymagany reset
**Rozwiązania**:
- Watchdog powinien automatycznie zrestartować po 120s
- Sprawdź logi Serial Monitor pod kątem błędów
- Zaktualizuj biblioteki do najnowszych wersji
- Zwiększ rozmiar stosu zadań w razie potrzeby

### Szybkie rozładowanie baterii
**Objawy**: Bateria rozładowuje się w kilka godzin
**Rozwiązania**:
- Zmniejsz `INACTIVITY_TIMEOUT` dla szybszego uśpienia
- Sprawdź, czy Deep Sleep działa poprawnie
- Zmniejsz częstotliwość odświeżania wyświetlacza
- Użyj większej baterii (zalecane: 1000+ mAh)
- Sprawdź pobór prądu multimetrem

### Nieprawidłowy odczyt napięcia baterii
**Objawy**: Wskaźnik baterii pokazuje błędne wartości
**Rozwiązania**:
- Sprawdź dzielnik napięcia (2x 10kΩ)
- Skalibruj `BATTERY_CALIBRATION_FACTOR` w kodzie
- Zmierz rzeczywiste napięcie multimetrem i porównaj
- Zwiększ `ADC_SAMPLES` dla lepszego filtrowania

### OTA nie działa
**Objawy**: Brak portu sieciowego w Arduino IDE
**Rozwiązania**:
- Upewnij się, że komputer i pilot są w tej samej sieci
- Sprawdź, czy pilot jest podłączony do WiFi
- Zrestartuj Arduino IDE
- Sprawdź firewall komputera (port mDNS 5353)

### Brakujące polskie znaki
**Objawy**: Zamiast ą,ć,ę,ł wyświetlają się znaki zapytania
**Rozwiązania**:
- Sprawdź, czy yoRadio wysyła dane w UTF-8
- Zweryfikuj funkcję `replacePolishChars()` w kodzie
- Sprawdź kodowanie pliku .ino (powinno być UTF-8)

## 📁 Struktura projektu

```
yoRadio_pilot/
├── yoRadio_pilot.ino      # Główny plik projektu
├── README.md              # Ten plik
├── LICENSE                # Licencja projektu
└── docs/                  # Dokumentacja (opcjonalnie)
    ├── schematic.png      # Schemat połączeń
    └── photos/            # Zdjęcia projektu
```

## 📝 Protokół WebSocket yoRadio

### Polecenia wysyłane DO radia
```json
{"command": "play"}
{"command": "stop"}  
{"command": "next"}
{"command": "prev"}
{"command": {"volume": 75}}
```

### Odpowiedzi OTRZYMYWANE z radia
```json
{
  "type": "title",
  "value": "Nazwa stacji##Tytuł##Artysta"
}
{
  "type": "station",
  "value": "1"
}
{
  "type": "bitrate",
  "value": "128"
}
{
  "type": "status",
  "value": 1  // 1=play, 2=stop
}
{
  "type": "volume",
  "value": 75
}
```

## 🤝 Wkład w projekt

Zgłoszenia błędów i pull requesty są mile widziane na GitHubie.

### Proces zgłaszania błędów
1. Sprawdź, czy błąd nie został już zgłoszony
2. Utwórz nowy Issue z opisem:
   - Wersja firmware
   - Kroki do reprodukcji
   - Oczekiwane zachowanie
   - Rzeczywiste zachowanie
   - Logi Serial Monitor (jeśli dostępne)

### Proces pull requestów
1. Fork repozytorium
2. Utwórz branch dla swojej funkcji (`git checkout -b feature/AmazingFeature`)
3. Commit zmian (`git commit -m 'Add some AmazingFeature'`)
4. Push do brancha (`git push origin feature/AmazingFeature`)
5. Otwórz Pull Request

## 📄 Licencja

Ten projekt jest dostępny na licencji MIT. Zobacz plik `LICENSE` dla szczegółów.

## 👨‍💻 Autor

**pimowo**
- GitHub: [@pimowo](https://github.com/pimowo)

## 🙏 Podziękowania

- [yoRadio](https://github.com/e2002/yoradio-full) - Wspaniały projekt internetowego radia
- Adafruit - Za świetne biblioteki graficzne
- Społeczność ESP32 - Za wsparcie i dokumentację

## 📊 Historia wersji

### v0.3 (2025-12-14)
- ✨ Dodano obsługę wielu rádií (przełączanie długim CENTER)
- ✨ Watchdog Timer (120s timeout)
- ✨ Detekcja timeout WebSocket z automatycznym reconnect
- ✨ Dynamiczny tryb 2/3 linii na podstawie długości tekstu
- ✨ Przewijanie sekwencyjne (najpierw tytuł, potem artysta)
- ✨ Bitrate tylko podczas odtwarzania
- ✨ Numer radia w lewym dolnym rogu (gdy NUM_RADIOS > 1)
- ✨ RSSI gdy NUM_RADIOS = 1
- 🐛 Poprawki stabilności połączenia WebSocket

### v0.2
- ✨ Dodano Deep Sleep po bezczynności
- ✨ Filtrowanie ADC dla pomiaru baterii
- ✨ Pełna obsługa polskich znaków UTF-8
- ✨ Ekran głośności z paskiem postępu
- 🐛 Poprawki wyświetlania i przewijania

### v0.1
- 🎉 Pierwsza wersja
- ✨ Podstawowe sterowanie WebSocket
- ✨ Wyświetlacz OLED z informacjami o stacji
- ✨ Enkoder i przyciski sterujące
- ✨ Monitorowanie baterii
- ✨ Aktualizacje OTA

---

**Pytania? Problemy?** Otwórz Issue na GitHubie!

**Enjoy your music! 🎵**
