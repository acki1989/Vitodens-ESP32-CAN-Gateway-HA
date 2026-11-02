# Vitodens E3-CAN Gateway (ESP32-C3 + SN65HVD230)

Ein ESP32-C3 liest eine **Viessmann Vitodens** direkt am internen E3-CAN-Bus aus und
stellt die Werte in Home Assistant bereit – ohne open3e-Server, ohne Raspberry Pi,
ohne Kabel zum PC. Zwei Stellwerte der Heizkurve lassen sich schreiben.

Das Gerät spricht **UDS über ISO-TP** (Service `0x22` lesen, `0x2E` schreiben) mit
250 kBit/s, also dasselbe Protokoll wie [open3e](https://github.com/open3e/open3e).
Die Byte-Layouts stammen aus dessen Datenpunktliste.

---

## ⚠️ Benutzung auf eigene Verantwortung

Dieses Projekt schreibt auf den **Steuerbus einer Heizungsanlage**. Fehler in
Hardware, Firmware oder Konfiguration können die Heizung stören, in einen
Störmodus bringen oder im schlimmsten Fall Sachschäden verursachen.

- Wer es nachbaut, tut das **auf eigene Verantwortung** und auf eigenes Risiko.
- Es gibt **keine Gewährleistung** und keinen Support-Anspruch – weder für die
  Funktion noch für Folgeschäden.
- Arbeiten an der Heizung dürfen nur Personen mit der nötigen Sachkenntnis
  ausführen. Im Zweifel: Fachbetrieb.
- Die Firmware ersetzt **keine** Herstellerwartung und keine Sicherheitsfunktion
  der Anlage.
- Keine Verbindung zu Viessmann oder zum open3e-Projekt.

Wer diese Hinweise nicht akzeptiert, sollte den Aufbau nicht nachbauen.

---

## Status

| | |
|---|---|
| Firmware | ESPHome 2026.10.1 / ESP-IDF 5.5.6, ca. 50 % Flash, 33 % RAM |
| Laufzeit | seit 01.11.2026 dauerhaft im Einsatz, 23 Datenpunkte, 0 Fehler |
| Letzte Dauerprüfung | 02.11.2026: 11.518 Abfragen ohne eine einzige Fehlantwort |
| Schreibpfad | Rundlauf 0,4 → 0,5 → 0,4 am Gerät bestätigt, jeweils `0x6E` |

---

## Hardware

| Teil | Anmerkung |
|---|---|
| ESP32-C3 SuperMini | 4 MB Flash, kein PSRAM – reicht für ESPHome + diese Logik |
| SN65HVD230 | 3,3-V-CAN-Transceiver, **kein** 5-V-MCP2515/TJA1050-Modul verwenden |

### Verdrahtung

```
SN65HVD230          ESP32-C3 SuperMini
VCC         3V3     (3,3 V – niemals 5 V)
GND         GND     gemeinsame Masse mit dem CAN-Bus ist Pflicht
CTX         GPIO4
CRX         GPIO5
Rs          GND     High-Speed-Modus (manche Module: Jumper)
CANH        E3-Bus CANH (gelbe Buchse 91, Pin H)
CANL        E3-Bus CANL (Pin L)
```

- **Busmasse** gehört an den GND-Pin der Buchse 91 (Beschriftung `L / GND / H`).
  Ohne sie fehlt dem Transceiver der Bezug: der TX-Fehlerzähler steigt, RX bleibt 0.
- **120 Ω** nur bestücken, wenn der Adapter am Busende sitzt. Zwei Widerstände zu
  viel legen den Bus lahm (äußert sich als Bus-Off).
- Freie GPIOs am C3: 0–10, 20, 21. **Nicht** verwenden: 18/19 (USB), 11–17 (Flash),
  2/8/9 (Strapping).
- Stromversorgung: eigenes 5-V-Netzteil oder Step-down. Nicht gleichzeitig USB eines
  geerdeten Rechners und CAN-Masse anklemmen (Erdungsschleife).

### Gehäuse (3D-Druck)

Unter [`hardware/gehaeuse/`](hardware/gehaeuse/) liegen die Druckvorlagen für ein
Gehäuse, in dem beide Platinen nebeneinander sitzen – Schraubklemme und USB-C
zeigen nach vorn.

| | |
|---|---|
| Außenmaß | 46,7 × 38,2 mm, 20 mm hoch (Boden 18 + Deckel 2) |
| Aufnahme | ESP32-C3 SuperMini 22,5 × 18 mm, SN65HVD230 28 × 11 mm |
| Aussparungen | Schraubklemme + Kabel (12,2 × 11,1 mm), USB-C (10,6 × 4,2 mm) |
| Druck | 0,2 mm Layer, 3 Wände, 15–20 % Infill, keine Stützen |

![Gehäuse für ESP32-C3 und SN65HVD230](hardware/gehaeuse/Vorschau.png)

Die ESP-Tasche ist für die nackte Platine (1,6 mm) ausgelegt – vorhandene
Stiftleisten vorher entfernen oder Kabel direkt anlöten. Maße, Montage und das
parametrische Quellskript stehen in
[`hardware/gehaeuse/README.md`](hardware/gehaeuse/README.md).

---

## Installation

**Voraussetzungen:** ESPHome 2026.10 oder neuer (Device-Builder-Add-on oder CLI),
ESP32-C3 SuperMini, SN65HVD230, ein USB-C-**Daten**kabel (kein reines Ladekabel)
und eine 3,3-V-Versorgung.

### 1. Dateien ablegen

| Datei | Ziel |
|---|---|
| `vitodens-can.yaml` | ESPHome-Konfigurationsordner |
| `vitodens-uds.h` | derselbe Ordner – wird per `includes: vitodens-uds.h` eingebunden |

Beim Device-Builder-Add-on ist das `/config/esphome`, bei einer CLI-Installation
der mit `--config` gewählte Ordner. Beide Dateien müssen **nebeneinander** liegen,
sonst bricht der Build mit `Could not find vitodens-uds.h` ab.

### 2. `secrets.yaml` ergänzen

Im selben Ordner (die Datei existiert meist schon):

```yaml
wifi_ssid: "MeinWLAN"
wifi_password: "…"
api_encryption_key: "…"   # 32 Byte, Base64
```

Schlüssel erzeugen mit `openssl rand -base64 32`. `secrets.yaml` steht in
`.gitignore` und gehört nicht ins Repo.

### 3. Prüfen und bauen

```bash
esphome config vitodens-can.yaml     # Schema + Includes prüfen
esphome compile vitodens-can.yaml
```

Im Device Builder: Gerät `vitodens-can` öffnen → **Install**. Der erste Build
dauert einige Minuten, weil ESP-IDF einmalig geladen wird.

### 4. Erstes Flashen (USB)

Device Builder: **Install → Plug into this computer** (Chrome/Edge, Web Serial).
Per CLI:

```bash
esphome run vitodens-can.yaml --device /dev/ttyUSB0   # ggf. /dev/ttyACM0
```

Wird das Board nicht erkannt: **BOOT** halten, **RST** kurz drücken, BOOT
loslassen und erneut flashen.

Während des Flashens hängt das Board am PC – CAN-Masse und PC-Erde nicht
gleichzeitig anklemmen (Erdungsschleife). Den Bus erst danach verbinden.

### 5. WLAN

Das Gerät nutzt die Zugangsdaten aus `secrets.yaml`. Klappt das nicht, öffnet es
den Fallback-Zugang **`Vitodens-CAN`** (Passwort `vitodens-can-2026`): damit
verbinden, `http://192.168.4.1` aufrufen und die WLAN-Daten eintragen.

### 6. In Home Assistant aufnehmen

Home Assistant meldet das Gerät meist selbst als entdeckt (ESPHome-Integration).
Sonst: **Einstellungen → Geräte & Dienste → Integration hinzufügen → ESPHome**,
Hostname/IP eintragen und den API-Schlüssel aus `secrets.yaml` hinterlegen.

Danach erscheinen die 58 Entitäten mit deutschen Namen; die endgültigen
Entitäts-IDs vergibt Home Assistant beim Anlegen.

### 7. Erster Funktionstest

| Prüfung | Erwartung |
|---|---|
| `CAN Bus Status` | `OK` |
| `CAN Anfragen OK` | steigt im 30-s-Takt |
| `CAN Fehler` | bleibt 0 |
| `Datenalter` | unter 60 s |
| `CAN Daten aktuell` | an |

`BUS_OFF` oder Timeouts bei allen DIDs: Verkabelung, Busmasse und Bitrate
prüfen – und sicherstellen, dass **kein zweiter Tester** (open3e) läuft.

### Updates

YAML ändern, dann `esphome run vitodens-can.yaml` (OTA) bzw. im Device Builder
**Install → Wirelessly**. Die USB-Erstinstallation ist nur einmal nötig.

---

## Protokoll

```
ESP32 ── 0x680 ──> ECU (HMUMASTER)
      <─ 0x690 ──
```

| | |
|---|---|
| Bus | E3-CAN, 250 kBit/s, klassisches CAN (kein CAN-FD) |
| Anfrage | `0x680`, Antwort `0x690` (ECU-Adresse + `0x10`) |
| Weitere ECUs | `0x684`/`0x694` und `0x688`/`0x698` antworten ebenfalls (per Scan bestätigt) |
| Empfangsfilter | `0x690…0x69F` |
| Dienste | UDS `0x22` (lesen), `0x2E` (schreiben), ISO-TP als Transport |

**Endianness – die häufigste Fehlerquelle:** Die DID-Wiederholung in der Antwort ist
**big-endian** (`62 01 0C …` für DID 268), die Nutzdaten *innerhalb* eines
Datenpunkts sind **little-endian**.

### Ablauf

Die Klasse arbeitet **nicht blockierend**: ein 20-ms-Intervall ruft `loop()`, das pro
Aufruf höchstens einen Schritt macht. WLAN und API bleiben dadurch reaktionsfähig.

| Parameter | Wert | Bedeutung |
|---|---|---|
| Abfragetakt | 30 s | 8 Messwerte (Temperaturen, Druck, Flamme, Pumpe, Modulation) |
| Abfragetakt | 300 s | 15 Werte (Statistik, Diagnose, Gateway) |
| Antwort-Timeout | 400 ms | danach gilt der DID als fehlgeschlagen, neuer Versuch in 10 s |
| Pause zwischen Anfragen | 25 ms | schont den Bus |
| Log-Bremse | 60 s je DID | Zähler laufen exakt weiter, nur das Log wird gedrosselt |

### Schreibpfad (Heizkurve, DID 880)

Das 4-Byte-Feld hat drei Teile: Byte 0 = **Steigung** (/10), Byte 1 = **Niveau**
(signed, 1 K), Byte 2–3 = **Fußpunkt** (int16 signed, /10).

1. DID 880 vollständig lesen (4 Byte)
2. **ein** Byte ersetzen – 0 für Steigung (`round(Steigung × 10)`), 1 für Niveau (`round(Niveau)`)
3. alle **4 Byte** per `0x2E` zurückschreiben (die anderen bleiben unverändert)
4. auf `0x6E` warten; `0x7F` (Negative Response) wird als Fehler mit NRC gemeldet
5. 2 s später wird DID 880 erneut gelesen – damit ist sichtbar, ob der Wert ankam

Maximal 3 Versuche mit 2 s Abstand, danach Abbruch. Der Fußpunkt ist nur lesbar.

---

## Datenpunkte (23)

| DID | open3e-Name | Byte | Inhalt | Einheit | Zugriff | Takt |
|---:|---|---:|---|---|---|---|
| 268 | `FlowTemperatureSensor` | 9 | 4× int16 /10 (Ist/Min/Max/Mittel) + Status | °C | ro | 30 s |
| 526 | `ModulationCurrentValue` | 2 | int16 /10 | % | ro | 30 s |
| 527 | `FlowTemperatureTargetSetpoint` | 2 | int16 /10 | °C | ro | 30 s |
| 274 | `OutsideTemperatureSensor` | 9 | wie 268 | °C | ro | 30 s |
| 331 | `FlueGasTemperatureSensor` | 9 | wie 268 | °C | ro | 30 s |
| 318 | `WaterPressureSensor` | 9 | wie 268 | bar | ro | 30 s |
| 364 | `Flame` | 6 | State, 2× raw, int16 /100, raw | µA | ro | 30 s |
| 381 | `CentralHeatingPump` | 4 | State, Soll-%, Ist, raw | % | ro | 30 s |
| 1346 | `HeatEngineStatistical` | 12 | 3× int32 | h / h / – | ro | 300 s |
| 548 | `EnergyConsumptionCentralHeating` | 24 | 6× int32 /10 | kWh | ro | 300 s |
| 544 | `GasConsumptionCentralHeating` | 12 | 6× int16 /10 | m³ | ro | 300 s |
| 1211 | `GeneratedCentralHeatingOutput` | 24 | 6× int32 /10 | kWh | ro | 300 s |
| 880 | `MixerOneCircuitCentralHeatingCurve` | 4 | Byte 0 Steigung /10, Byte 1 Niveau, Byte 2–3 Fußpunkt /10 | – | **rw** | 300 s |
| 929 | `GasType` | 1 | Enum (1 LLGas, 2 EGas, 3 Flüssiggas) | – | ro | 300 s |
| 901 | `ServiceManagerIsRequired` | 1 | Byte ≠ 0 = Wartung | – | ro | 300 s |
| 902 | `MalfunctionIdentification` | 1 | Byte ≠ 0 = Störung | – | ro | 300 s |
| 522 | `ServiceDateNext` | 4 | Tag, Monat, 2000+Jahr, Status | Datum | ro | 300 s |
| 265 | `ErrorDtcList` | 122 | uint16 Anzahl + n× 12 Byte | – | ro | 300 s |
| 1165 | `BackendConnectionStatus` | 1 | 3 = verbunden | – | ro | 300 s |
| 593 | `GatewayMac` | 6 | MAC des Internetmoduls | – | ro | 300 s |
| 607 | `GatewayRemoteIp` | 20 | 5× IPv4, erstes = WLAN-IP | – | ro | 300 s |
| 602 | `GatewayRemoteLocalNetworkStatus` | 1 | 3 = verbunden | – | ro | 300 s |
| 900 | `GatewayRemoteSignalStrength` | 1 | 0–100 | % | ro | 300 s |

### Abweichungen von den open3e-Metadaten

- **DID 318**: open3e deklariert `hPa`, geliefert wird der Bar-Wert (1,4) – hier als `bar`.
- **DID 880**: open3e führt das Feld als `acc="ro"`, die ECU akzeptiert Schreibzugriffe aber.
- **DID 593/607/602/900/1165** beschreiben das **eingebaute Viessmann-Internetmodul**
  (MAC-OUI `B8:74:24` = Viessmann Elektronik), nicht dieses Gateway.
- **DID 265**: hier nur Anzahl und Fehler-IDs, keine Klartextnamen – dafür müsste die
  komplette `Errors`-Enum mit auf das Gerät.

Vollständige Referenz mit Beispielwerten: [`docs/dids-heizung.md`](docs/dids-heizung.md).

---

## Entitäten (58)

| Gruppe | Anzahl | Inhalt |
|---|---|---|
| Sensoren | 31 | Temperaturen, Druck, Modulation, Betriebs-/Brennerstunden, Starts, je 4× Energie/Gas/Wärme, Ionisationsstrom, Pumpenwerte, Heizkurve, Fehleranzahl, Gateway-Diagnose, Datenalter, CAN-Zähler |
| Textsensoren | 14 | Sensorstatus (4×), Gastyp, Servicetermin, Fehlerliste, Gateway-MAC/-IP/-Netz, CAN-Status, letzter Fehler, Schreibstatus, Bus-IDs |
| Binärsensoren | 6 | Brennerflamme, Heizkreispumpe, Wartung, Störung, Backend, CAN-Daten-aktuell |
| Zahlen | 4 | Heizkurve Steigung/Niveau (Gerät), Neigung/Niveau (Entwurf) |
| Buttons | 2 | CAN-Bus neu starten, Bus-Scan starten |
| Schalter | 1 | Schreiben freigeben |

### Verhalten bei veralteten Daten

Jeder DID merkt sich den Zeitpunkt der letzten erfolgreichen Antwort. Ist er älter als
5 Abfragezyklen (mindestens 2 Minuten), gilt der Wert als veraltet:

- numerische Sensoren senden **nichts** mehr (der letzte Wert bleibt stehen)
- Textsensoren zeigen `veraltet`
- `CAN Daten aktuell` geht auf *aus*, `Datenalter` zählt die Sekunden

Bewusst **kein** `NAN`: Home Assistant wirft bei Sensoren mit `device_class`/`state_class`
einen `ValueError` („non-finite value") und protokolliert das bei jedem Update.

---

## Konfiguration

```yaml
substitutions:
  can_tx_pin: "4"     # CTX des Transceivers
  can_rx_pin: "5"     # CRX des Transceivers
```

Takte, Timeouts und Grenzen stehen als Konstanten am Anfang von `vitodens-uds.h`:

```cpp
static constexpr uint32_t REQUEST_TIMEOUT_MS      = 400;
static constexpr uint32_t GAP_MS                  = 25;
static constexpr uint32_t FAST_PERIOD_MS          = 30000;
static constexpr uint32_t SLOW_PERIOD_MS          = 300000;
```

Weiteren Datenpunkt ergänzen: Eintrag in `jobs_[]` (Periode wählen), `case` in
`decode_()`, Feld in `Values`, Entität in der YAML.

---

## Betrieb und Sicherheit

1. **Nur ein Tester am Bus.** Vor dem Start `open3e` (Docker/Add-on) stoppen. Zwei
   Tester erzeugen Kollisionen und falsche Werte.
2. **Schreiben ist standardmäßig gesperrt.** Der Schalter *Schreiben freigeben* steht
   nach jedem Neustart auf *aus* (`restore_mode: ALWAYS_OFF`). Erst einschalten, dann
   die Heizkurve ändern – so kann auch ein Sprachassistent nichts verstellen.
3. **Wertebereich** der Steigung 0,2–3,5, des Niveaus −13…40 K. Die Firmware prüft
   beides selbst und lehnt Werte außerhalb ab, statt sie zu klemmen.
4. **Nicht als Ersatz für Herstellerwartung** oder Sicherheitsfunktionen verwenden.

---

## Diagnose

| Entität | Aussage |
|---|---|
| `CAN Bus Status` | `OK` / `BUS_OFF` / `STOPPED` / `RECOVERING` (TWAI-Zustand) |
| `CAN Anfragen OK` / `CAN Fehler` | Zähler seit dem Start |
| `CAN letzter Fehler` | letzter Fehlschlag inkl. NRC, z. B. `NRC 0x31 bei DID 0x10C` |
| `Datenalter` | Sekunden seit der letzten gültigen Antwort |
| `CAN Daten aktuell` | mindestens ein Wert frisch |
| `Schreibstatus` | `idle` / `laeuft` / `Versuch 2: …` / `abgebrochen: …` / `ok` |
| `Bus-IDs (Diagnose)` | Ergebnis des Adress-Scans |
| Button `Bus-Scan starten` | klopft `0x680..0x6EF` mit DID 256 ab (~20 s) |

Häufige Negative Response Codes:

| NRC | Bedeutung | typische Ursache |
|---|---|---|
| `0x22` | conditionsNotCorrect | falsche Betriebsart |
| `0x31` | requestOutOfRange | DID existiert an dieser ECU nicht |
| `0x33` | securityAccessDenied | Schreibzugriff gesperrt |
| `0x78` | responsePending | Gerät braucht länger (kein Fehler) |

`Timeout` bei **allen** DIDs heißt: gesendet und quittiert, aber keine Antwort –
Verdacht auf falsche ECU-Adresse (Adress-Scan nutzen). `Senden fehlgeschlagen` bei
allen DIDs heißt: niemand quittiert – Verkabelung, Masse oder Bitrate prüfen.

---

## Home-Assistant-Begleitung (optional)

**Dashboard `heizung-kennlinie`** mit drei Views:

| View | Inhalt |
|---|---|
| Heizkurve | Plotly-Kurve (Ist/Entwurf), Stützpunkt-Tabelle, Entwurfs-Tasten, Gerätewerte, Übernehmen/Verwerfen |
| Verbrauch | Gasverbrauch pro Tag (Statistik `max`), Brenner/Pumpe 24 h, Tageswerte |
| Brenner | Außen/Vorlauf/Modulation über 24 h (zwei Y-Achsen), Flamme 7 Tage, Starts/Tag |

**Skripte** (UI-Skripte, nicht YAML):

| Skript | Aufgabe |
|---|---|
| `heizkurve_uebernehmen` | schreibt den Entwurf aufs Gerät, prüft vorher die Schreibfreigabe |
| `heizkurve_verwerfen` | setzt den Entwurf auf die Gerätewerte |
| `heizkurve_neigung_schritt` | ±0,1 je Schritt, geklemmt auf 0,2…3,5 |
| `heizkurve_niveau_schritt` | ±1 je Schritt, geklemmt auf −13…40 |

**Zwei YAML-Sensoren** in `configuration.yaml` (Brenner-Diagnose):

```yaml
sensor:
  - platform: statistics
    name: "Brennerstarts 24 h"
    entity_id: sensor.vitodens_can_brennerstarts
    state_characteristic: change
    max_age:
      hours: 24
    sampling_size: 500

  - platform: history_stats
    name: "Brennerlaufzeit heute"
    entity_id: binary_sensor.vitodens_can_brennerflamme
    state: "on"
    type: time
    start: "{{ today_at('00:00') }}"
    end: "{{ now() }}"
```

**Energie-Dashboard:** `sensor.vitodens_can_gasverbrauch_heizung_jahr` ist als Gasquelle
eingetragen (`device_class: gas`, `state_class: total_increasing`, m³). Der Zähler läuft
innerhalb des Jahres und springt am 1. Januar auf 0 – Home Assistant behandelt das als
Zählerwechsel und rechnet weiter.

Die Screenshots unter `docs/` zeigen die ViCare-Referenzkurve (Neigung 0,4 und 0,8
bei Niveau 3 bzw. 4) sowie die gefahrenen Raumtemperaturen – daran wurde das
Kurvenmodell kalibriert.

Für die Kurve wird `plotly-graph-card` benötigt (HACS oder manuell unter
`/config/www/community/plotly-graph-card/`), als Lovelace-Ressource vom Typ
`JavaScript-Modul`.

---

## Stolperfallen (alle selbst durchlaufen)

1. **Die DID-Wiederholung in einer UDS-Antwort ist big-endian.** Der teuerste Fehler:
   `62 01 0C …` für DID 268 wurde mit `le_u16()` als `0x0C01` gelesen, passte nie und
   **jede gültige Antwort wurde verworfen** – sichtbar nur als Timeout, obwohl ACK und
   Antwort kamen. Nutzdaten *innerhalb* eines Datenpunkts sind dagegen little-endian.
2. **`driver/twai.h` fehlt bei ESPHome 2026.10.** ESPHome schließt die IDF-Treiber aus,
   beide Komponenten müssen wieder aktiviert werden:
   ```yaml
   framework:
     type: esp-idf
     advanced:
       include_builtin_idf_components:
         - driver           # Legacy-Shim mit driver/twai.h
         - esp_driver_twai  # eigentliche Implementierung (IDF 5.5)
   ```
3. **`globals` mit eigenem C++-Typ scheitert.** ESPHome generiert die Global-Speicher
   *vor* dem `includes:`-Block, der Typ ist dort noch unbekannt. Lösung: Zugriffsfunktion
   mit statischer Instanz (`vitodens::bus()`).
4. **`NAN` an Home-Assistant-Sensoren ist keine gute Idee** – siehe „veraltete Daten".
5. **Ein Timeout ohne `job_done_` blockiert alles.** Bleibt der Job „überfällig", fragt
   der Scheduler denselben DID im 25-ms-Takt erneut und kommt nie zu den anderen.
6. **Schreibzustand bei Abbrüchen aufräumen.** Wird nur der Empfangszustand
   zurückgesetzt (Bus-Recovery, verlorenes Frame), bleibt der Schreibvorgang „laufend".
7. **`total_increasing` nur für monotone Werte.** Bei rollierenden Fenstern (7 Tage,
   Monat, Jahr) interpretiert HA jeden Rückgang als Zählerrücksetzung und bucht
   Phantomverbrauch – dort `measurement`, aber nie zusammen mit `device_class: energy`.
8. **`O3EInt16` hat in open3e die Standard-Skalierung 10**, `O3EInt8`/`O3EInt32` dagegen 1.
   Wer den Fußpunkt ohne `/10` liest, bekommt −300 statt −30 °C.
9. **Diagnose per Adress-Scan.** Kommen auf Anfragen nur Timeouts, obwohl der Bus ACKs
   liefert: alle Adressen `0x680..0x6EF` mit DID 256 (`BusIdentification`) abklopfen.
   Genau so wurde Fehler 1 gefunden – und bestätigt, dass `0x680`, `0x684` und `0x688`
   alle antworten.

---

## Grenzen und offene Punkte

- Ziel-ECU ist `0x680`. `0x684`/`0x688` antworten zwar, sind aber nicht angebunden –
  dafür müsste die ECU-Adresse pro DID konfigurierbar werden.
- Fehlerliste ohne Klartextnamen (siehe DID 265).
- Zwei Stellwerte auf DID 880 (Steigung, Niveau); der Fußpunkt wird nur gelesen.
  Weitere `rw`-Datenpunkte wären z. B. 396 (WW-Soll), 537/538 (externe Betriebsart).
- Kein Tester-Present, keine Session-Steuerung – für `0x22`/`0x2E` im Default-Session
  nicht nötig.
- Der Gaszähler ist der Jahreswert; ein jahresübergreifender Zähler existiert nicht.

---

## Verifikation

| Prüfung | Ergebnis |
|---|---|
| Byte-Layouts | gegen `Open3Edatapoints.py` abgeglichen |
| C++ | `g++ -Wall -Wextra` gegen IDF-Stubs, ohne Warnungen |
| Firmware | ESPHome 2026.10.1 / IDF 5.5.6, wiederholt gebaut und per OTA geflasht |
| Laufzeit | 23 DIDs, 0 Fehler, 0 Timeouts über Nacht (11.518 Abfragen) |
| Plausibilität | Vorlauf 36,7 °C · außen 5,5 °C · Modulation 14,2 % · Druck 1,5 bar · Betriebsstunden 25.921 h |
| Gegenprobe open3e | identische Werte für Druck (1,4), Heizkurve (0,4), Gateway-MAC, WLAN-IP, Wartung |
| Schreibpfad | Rundlauf 0,4 → 0,5 → 0,4, jeweils `0x6E` und korrekt zurückgelesen |
| Statistik | Tagesgas 01.11. 5,9 m³ / 02.11. 0,8 m³; 8 Brennerstarts, ~37 min je Lauf |

---

## Lizenz

Apache License 2.0 – siehe [`LICENSE`](LICENSE) und [`NOTICE`](NOTICE).
Copyright 2026 acki1989.

Die Datenpunktdefinitionen (DID-Nummern, Byte-Layouts, Skalierungen, Enum-Werte)
stammen aus [open3e](https://github.com/open3e/open3e) (Apache-2.0). Es wurde kein
Quellcode von open3e übernommen.

## Haftung

Code und Dokumentation ohne Gewähr. Die Benutzung erfolgt **auf eigene Verantwortung
und auf eigenes Risiko**, insbesondere Schreibzugriffe auf eine Heizungsanlage.
Keine Verbindung zu Viessmann oder zum open3e-Projekt. Für Schäden, die aus dem
Nachbau oder Betrieb entstehen, wird keine Haftung übernommen.
