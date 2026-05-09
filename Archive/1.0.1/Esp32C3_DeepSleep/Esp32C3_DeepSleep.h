#pragma once
#include <Arduino.h>
#include "esp_sleep.h"

namespace Esp32C3_DeepSleep {
    void addWakeupPin(uint8_t gpioPin, bool wakeOnHigh);
    void clearWakeupPins();
    void beginTimerWakeup(uint64_t timeUs);
    // configurePowerDomains() wurde entfernt
    void holdGPIO(uint8_t gpioPin);
    void releaseAllHolds();
    esp_sleep_wakeup_cause_t wakeupCause();

    /**
     * @brief Enters deep sleep immediately (does not return).
     * Automatically manages RTC power domains for optimal power consumption
     * based on the configured wakeup sources.
     */
    [[noreturn]] void goDeepSleep();
}