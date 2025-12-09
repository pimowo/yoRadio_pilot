# YoRadio OLED Display Controller

Projekt sterownika wyświetlacza OLED dla radioodbiornika YoRadio.  Obsługuje wyświetlanie informacji o stacji, wykonawcy, utworze oraz dynamiczne przewijanie tekstu.

## Funkcjonalność

### Wyświetlanie informacji
- **Linia 1 (Stacja)** - nazwa stacji radiowej na czarnym tle
- **Linia 2 (Wykonawca)** - imię i nazwisko wykonawcy
- **Linia 3 (Utwór)** - tytuł utworu
- **Dolna linia** - RSSI (sygnał WiFi), bateria, głośność, bitrate

### Przewijanie tekstu
- Tekst przewija się **sekwencyjnie** - linia 1 → linia 2 → linia 3 → linia 1
- Każda linia ma **własny timer** - niezależne przewijanie
- **Pauza na początku** (1500ms) przed rozpoczęciem przewijania
- Separator " * " między powtórzeniami tekstu
- Obsługa **polskich znaków** (ą, ć, ę, ł, ń, ó, ś, ź, ż)

### Stany WiFi
- **Łączenie** - animacja barek + nazwa SSID (wyśrodkowane)
- **Błąd** - ikona WiFi (mrugająca) + tekst "Brak WiFi"
- **Połączono** - normalne wyświetlanie informacji

### Sterowanie przyciskami
- **BTN_UP (pin 7)** - głośność w górę
- **BTN_DOWN (pin 3)** - głośność w dół
- **BTN_CENTER (pin 5)** - odtwórz/pauza
- **BTN_LEFT (pin 6)** - poprzedni utwór (krótkie naciśnięcie) / poprzednie radio (długie naciśnięcie 2s)
- **BTN_RIGHT (pin 4)** - następny utwór (krótkie naciśnięcie) / następne radio (długie naciśnięcie 2s)

### Multi-Radio Support (v0.3+)
System obsługuje do 9 radioodbiorników yoRadio jednocześnie:

#### Konfiguracja
```cpp
const char* RADIO_IPS[] = {
  "192.168.1.101",  // Radio #1
  "192.168.1.102",  // Radio #2
  "192.168.1.103"   // Radio #3
};
// NUM_RADIOS jest automatycznie obliczane z rozmiaru tablicy
```

#### Przełączanie między radiami
- **LEFT (długie 2s)** - przełącz na poprzednie radio
- **RIGHT (długie 2s)** - przełącz na następne radio
- Akcja wykonuje się dokładnie po 2 sekundach trzymania przycisku
- Tylko jedna akcja na cykl naciśnięcia (wymaga puszczenia przed kolejną)
- Nie działa podczas deep sleep
- Nie można przełączyć poniżej radio #1 (akcja ignorowana)
- Nie można przełączyć powyżej ostatniego skonfigurowanego radia (akcja ignorowana)

#### Wskaźnik radia
- Numer radia wyświetlany w prawym górnym rogu linii 2 (artysta/utwór)
- Format: ` x ` (z spacjami), gdzie x = numer radia (1-9)
- Styl: czcionka rozmiar 1, tryb negatywowy (białe tło, czarny tekst)
- Wyświetlane tylko gdy NUM_RADIOS > 1
- Szerokość tekstu utworu automatycznie redukowana o 18 pikseli

#### Persystencja
- Wybrany numer radia zapisywany w pamięci RTC
- Automatycznie przywracany po restarcie i wybudzeniu z deep sleep
- Walidacja przy starcie (reset do 0 jeśli nieprawidłowy)

#### WebSocket
- Automatyczne rozłączenie ze starym radiem
- Automatyczne połączenie z nowym radiem
- Stan połączenia prawidłowo zarządzany
- Czyszczenie stanu wyświetlacza przy przełączaniu

## Hardware

### Wyświetlacz
- **OLED 128x64** (SSD1306)
- Interfejs I2C (adres: 0x3C)
- Zasilanie: 3.3V / 5V

### Mikrokontroler
- ESP32 lub kompatybilny
- Piny I2C: SDA, SCL
- Piny przycisków: 3, 4, 5, 6, 7

### Komunikacja
- **WiFi** - połączenie do routera
- **WebSocket** - komunikacja z YoRadio (port 80)

## Wdrażanie

### Wymagane biblioteki
