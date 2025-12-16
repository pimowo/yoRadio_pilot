#ifndef TYPES_H
#define TYPES_H

// ===== WIFI STATE ENUM =====
enum WifiState { 
  WIFI_CONNECTING, 
  WIFI_ERROR, 
  WIFI_OK 
};

// ===== SCROLL CONFIGURATION =====
struct ScrollConfig {
  int left;
  int top;
  int fontsize;
  int width;
  unsigned long scrolldelay;
  int scrolldelta;
  unsigned long scrolltime;
};

// ===== SCROLL STATE =====
struct ScrollState {
  int pos;
  unsigned long t_last;
  unsigned long t_start;
  bool scrolling;
  bool isMoving;
  String text;
  int singleTextWidth;
  int suffixWidth;
};

// ===== BATTERY FILTER CLASS (EMA) =====
class BatteryFilter {
  private:
    float filtered;
    bool initialized;
  public:
    BatteryFilter() : initialized(false) {}
    float update(float raw) {
      if (!initialized) {
        filtered = raw;
        initialized = true;
      } else {
        filtered = 0.15 * raw + 0.85 * filtered;
      }
      return filtered;
    }
};

#endif // TYPES_H
