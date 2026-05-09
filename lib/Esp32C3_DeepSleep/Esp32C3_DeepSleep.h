/*
 * =======================================================
 * Esp32C3_DeepSleep.h (Verbesserte Version)
 * =======================================================
 * Schnittstellendatei für die Deep Sleep Bibliothek.
 */

#pragma once
#include <Arduino.h>
#include "esp_sleep.h"

namespace Esp32C3_DeepSleep {
    /**
     * @brief Fügt einen GPIO-Pin als Weckquelle hinzu.
     * @param gpioPin Der Pin, der als Weckquelle dienen soll.
     * @param wakeOnHigh true, wenn bei HIGH aufgeweckt werden soll, false bei LOW.
     * @note Alle Pins müssen denselben Weck-Level (HIGH oder LOW) haben.
     */
    void addWakeupPin(uint8_t gpioPin, bool wakeOnHigh);

    /**
     * @brief Entfernt alle konfigurierten GPIO-Weckquellen.
     */
    void clearWakeupPins();
    
    /**
     * @brief Konfiguriert den Timer als Weckquelle.
     * @param timeUs Die Schlafdauer in Mikrosekunden.
     */
    void beginTimerWakeup(uint64_t timeUs);

    /**
     * @brief Erzwingt das Anlassen des RTC-Speichers im nächsten Schlafzyklus.
     * @param keepOn true, um den RTC-Speicher aktiv zu halten (z.B. für Zähler bei GPIO-Wakeup).
     */
    void keepRtcMemory(bool keepOn);

    /**
     * @brief Hält den aktuellen Zustand eines GPIO-Pins während des Deep Sleep.
     * @param gpioPin Der Pin, dessen Zustand gehalten werden soll.
     */
    void holdGPIO(uint8_t gpioPin);

    /**
     * @brief Löst den "Hold"-Zustand für alle GPIO-Pins.
     */
    void releaseAllHolds();
    
    /**
     * @brief Gibt den Grund für das letzte Aufwachen zurück.
     * @return Die esp_sleep_wakeup_cause_t Enumeration.
     */
    esp_sleep_wakeup_cause_t wakeupCause();

    /**
     * @brief Versetzt den ESP32 sofort in den Deep Sleep (kehrt nicht zurück).
     * Verwaltet automatisch die RTC-Power-Domains für optimalen Stromverbrauch,
     * basierend auf den konfigurierten Weckquellen und manuellen Einstellungen.
     */
    [[noreturn]] void goDeepSleep();
}