#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_ADXL345_U.h>
#include <Esp32C3_DeepSleep.h>

// --- KONFIGURATION ---
// Eindeutige ID für den Sensor
#define SENSOR_ID 12345
// GPIO-Pin am ESP32-C3, der mit INT1 des ADXL345 verbunden ist.
#define WAKEUP_PIN GPIO_NUM_2 

// KORREKTUR: Empfindlichkeit als Wert von 0 bis 100.
// 0   = sehr empfindlich
// 100 = sehr unempfindlich
// Ein guter Startwert ist 50.
#define SENSITIVITY 10

// --- GLOBALE VARIABLEN ---
// Sensor-Objekt aus der Adafruit-Bibliothek
Adafruit_ADXL345_Unified accel = Adafruit_ADXL345_Unified(SENSOR_ID);

// Zähler, der im RTC-Speicher gespeichert wird und den Tiefschlaf überlebt
RTC_DATA_ATTR int bootCount = 0;

// --- FUNKTIONSPROTOTYPEN ---
void print_wakeup_reason();
void configureADXL345();
void goToDeepSleep();

// --- SETUP-FUNKTION ---
// Wird einmal nach dem Start oder nach dem Aufwachen aus dem Tiefschlaf ausgeführt
void setup() {
  Serial.begin(115200);
  delay(1000); // Zeit geben, um den Seriellen Monitor zu öffnen

  // I2C-Pins explizit initialisieren, passend zu deinem Aufbau.
  Wire.begin(7, 6); // SDA = GPIO7, SCL = GPIO6

  // Boot-Zähler erhöhen und ausgeben
  ++bootCount;
  Serial.println("Boot-Nummer: " + String(bootCount));

  // Grund für das Aufwachen ausgeben
  print_wakeup_reason();

  // ADXL345-Sensor konfigurieren
  configureADXL345();
  Serial.println("ADXL345 für Aktivitäts-Interrupt (aktiv-LOW) konfiguriert.");

  // ESP32 in den Tiefschlaf versetzen
  goToDeepSleep();
}

// --- LOOP-FUNKTION ---
// Diese Funktion wird nie erreicht, da das Gerät am Ende von setup() in den Tiefschlaf geht
void loop() {
  // Leerer Loop
}

// --- HELFERFUNKTIONEN ---

// Konfiguriert den ADXL345 für den bewegungsaktivierten Interrupt
void configureADXL345() {
  if (!accel.begin()) {
    Serial.println("ADXL345 nicht gefunden! Verkabelung prüfen.");
    while (1) delay(10); // Endlosschleife
  }
  
  // Setzt den Messbereich. Für die reine Bewegungserkennung ist der Bereich weniger kritisch.
  accel.setRange(ADXL345_RANGE_16_G);
  
  // --- Direkte Registerkonfiguration für den Aktivitäts-Interrupt ---

  // KORREKTUR: Berechnet den Registerwert aus dem SENSITIVITY-Wert (0-100).
  // Der Wertebereich (10-120) hat sich in der Praxis als gut erwiesen.
  uint8_t activityThreshold = map(SENSITIVITY, 0, 100, 10, 120);
  Serial.printf("Empfindlichkeit: %d -> Registerwert: %d\n", SENSITIVITY, activityThreshold);
  
  // Setzt die berechnete Aktivitätsschwelle.
  accel.writeRegister(ADXL345_REG_THRESH_ACT, activityThreshold);
  
  // Konfiguriert die Achsen für die Aktivitätserkennung (X, Y, Z, DC-gekoppelt)
  accel.writeRegister(ADXL345_REG_ACT_INACT_CTL, 0x7F);
  
  // Aktiviert den ACTIVITY-Interrupt in der Interrupt-Enable-Maske
  accel.writeRegister(ADXL345_REG_INT_ENABLE, 0x10); // Bit 4 = ACTIVITY
  
  // Mappt den ACTIVITY-Interrupt auf den INT1-Pin (alle Bits auf 0 = INT1)
  accel.writeRegister(ADXL345_REG_INT_MAP, 0x00);
  
  // KRITISCHER SCHRITT: Setzt die Interrupt-Polarität auf aktiv-LOW
  uint8_t data_format = accel.readRegister(ADXL345_REG_DATA_FORMAT);
  data_format |= 0b00100000;
  accel.writeRegister(ADXL345_REG_DATA_FORMAT, data_format);

  // Löscht die Interrupt-Quelle zu Beginn, um sicherzustellen, dass keine alten
  // Ereignisse den Pin LOW halten.
  uint8_t intSource = accel.readRegister(ADXL345_REG_INT_SOURCE);
  Serial.print("Initialer INT_SOURCE-Wert: 0x");
  Serial.println(intSource, HEX);
}

// Konfiguriert und startet den Tiefschlafmodus
void goToDeepSleep() {
  Serial.println("Konfiguriere Wecksignal und gehe in den Tiefschlaf...");
  Serial.flush(); // Sicherstellen, dass alle seriellen Ausgaben gesendet wurden

  // Internen Pull-up für den Weck-Pin aktivieren.
  // Das sorgt für einen stabilen HIGH-Pegel, bis der ADXL345 ihn auf LOW zieht.
  pinMode(WAKEUP_PIN, INPUT_PULLUP);

  // GPIO als Weckquelle für den ESP32-C3 aktivieren
  // Trigger bei einem LOW-Pegel am WAKEUP_PIN
  esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKEUP_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  
  // Tiefschlaf starten
  esp_deep_sleep_start();
  
  // Code nach esp_deep_sleep_start() wird nicht ausgeführt
  Serial.println("Dies wird niemals gedruckt.");
}

// Gibt den Grund für das Aufwachen aus dem Tiefschlaf aus
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_GPIO:
      Serial.println("Weckgrund: Externes Signal über GPIO");
      break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Weckgrund: Timer");
      break;
    default:
      Serial.printf("Weckgrund: Nicht durch Tiefschlaf verursacht (Code: %d)\n", wakeup_reason);
      break;
  }
}
