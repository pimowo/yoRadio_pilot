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
- **BTN_LEFT (pin 6)** - poprzedni utwór
- **BTN_RIGHT (pin 4)** - następny utwór

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
