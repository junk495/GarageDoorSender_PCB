Zusammenfassung: Meilenstein 2
Projekt-Übersicht
Das Projekt ist ein autarker, batteriebetriebener Sensor-Knoten. Es erfasst Umgebungs- und Lage-Daten und kann zusätzlich über einen Fingerabdruck-Sensor bedient werden. Alle Ereignisse werden drahtlos via ESP-NOW an eine Basisstation gesendet, wobei der Stromverbrauch durch Deep Sleep und gezieltes Schalten von Komponenten minimiert wird.

Kernfunktionen des Geräts
Dualer Wakeup: Das Gerät wacht unter zwei Bedingungen auf:
Timer: Automatisch alle 5 Sekunden, um Umgebungsdaten zu senden.
Finger-Berührung: Sofort, wenn der R503-Sensor berührt wird.
Zwei Ereignis-Typen: Je nach Weckgrund werden unterschiedliche Datenpakete via ESP-NOW gesendet:
SensorMessage: Enthält Temperatur, Luftfeuchtigkeit, absolute Feuchtigkeit und den Torstatus. Wird bei Timer-Wakeup gesendet.
FingerEvent: Enthält die erkannte Finger-ID, die Erkennungssicherheit (Confidence) und den Torstatus. Wird bei Fingerprint-Wakeup gesendet.
Intelligentes Power-Management:
Der R503 Fingerprint-Sensor wird nur bei Bedarf über einen MOSFET mit Hauptstrom versorgt.
Das WiFi-Modul wird ebenfalls nur für den kurzen Sendevorgang aktiviert und sofort danach wieder abgeschaltet.
Interaktives Feedback: Der Fingerprint-Scan wird von einer RGB-LED begleitet (blaues Atmen bei Bereitschaft, grün bei Erfolg, rot bei Fehlschlag) und hat einen Timeout von 10 Sekunden.
Lageerkennung: Der "Torstatus" (0, 1 oder 2) wird aus dem Winkel der Z-Achse des ADXL345-Beschleunigungssensors berechnet.
Die Esp32C3_DeepSleep Bibliothek - Unsere Funktionen
Wir haben eine eigene kleine Bibliothek erstellt, die uns die Ansteuerung der ESP32-C3 Deep-Sleep-Funktionen vereinfacht. Ihre öffentlichen Funktionen sind:

addWakeupPin(pin, level): Konfiguriert einen GPIO als Weckquelle.
clearWakeupPins(): Löscht die GPIO-Weckquellen.
beginTimerWakeup(zeit_us): Konfiguriert den Timer als Weckquelle und sorgt dafür, dass der RTC-Speicher an bleibt.
holdGPIO(pin): "Friert" den aktuellen Zustand eines RTC-Pins für den Deep Sleep ein (z.B. um den R503-Strom im Schlaf abzuschalten).
releaseAllHolds(): Gibt alle gehaltenen Pins nach dem Aufwachen wieder frei.
wakeupCause(): Gibt den Grund für das Aufwachen zurück (Timer, GPIO, etc.).
goDeepSleep(): Startet den Deep Sleep und managt automatisch die Stromversorgung des RTC-Speichers.