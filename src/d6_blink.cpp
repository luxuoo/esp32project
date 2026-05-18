#include "d6_blink.h"
#include "globals.h"

void startD6Blink(int times) {
  d6_blink_remaining = times * 2;
  d6_blinking = true;
  d6_blink_state = false;
  lastD6BlinkTime = millis();
}

void updateD6Blink() {
  if (!d6_blinking) return;
  if (millis() - lastD6BlinkTime >= 200) {
    d6_blink_state = !d6_blink_state;
    digitalWrite(PIN_D6, d6_blink_state ? HIGH : LOW);
    lastD6BlinkTime = millis();
    if (--d6_blink_remaining <= 0) {
      d6_blinking = false;
      if (!sensor_error) digitalWrite(PIN_D6, LOW);
    }
  }
}
