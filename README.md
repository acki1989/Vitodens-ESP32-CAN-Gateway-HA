# Vitodens E3-CAN Gateway (ESP32-C3 + SN65HVD230)

Ein ESP32-C3 liest eine Viessmann **Vitodens** direkt am internen E3-CAN-Bus aus und
stellt die Werte in Home Assistant bereit — ohne open3e-Server, ohne Raspberry Pi,
ohne Kabel zum PC. Ein schreibbarer Datenpunkt (Heizkurven-Steigung) ist enthalten.

Das Gerät spricht **UDS über ISO-TP** (Service `0x22` lesen, `0x2E` schreiben) mit
250 kBit/s, also genau das Protokoll, das auch [open3e](https://github.com/open3e/open3e)
verwendet. Die Byte-Layouts sind aus `Open3Edatapoints.py` übernommen.

**Status**

| | |
|---|---|
| Firmware | ESPHome 2026.10.1 / ESP-IDF 5.5.6, Flash 49,9 %, RAM 32,9 % |
| Laufzeit am echten Bus | **seit 01.11.2026 im Einsatz** – 22 DIDs, 0 Fehler, 0 Timeouts |
| Gegenprobe | Werte identisch mit der open3e-HA-Integration (Wasserdruck 1,4 bar, Heizkurve 0,4, Gateway-MAC, WLAN-IP) |
| Schreibpfad DID 880 | über die open3e-Integration verifiziert (0,7 gesetzt und zurückgelesen); mit dieser Firmware noch nicht ausgelöst |

---

## Was es nicht ist

- Kein Ersatz für open3e. Es werden **22 ausgewählte Datenpunkte** gelesen, nicht die
  komplette Datenbank (mehrere tausend DIDs).
- Kein Bus-Master-Ersatz: solange das Gerät läuft, darf **kein** zweiter Tester
  (open3e, Adapter am Unraid, ioBroker) auf demselben Bus Anfragen stellen.

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
CANH        E3-Bus CANH (gelbe Buchse am Gerät)
CANL        E3-Bus CANL
```

- **120 Ω** nur bestücken, wenn der Adapter physikalisch am Busende sitzt. Zwei
  Abschlusswiderstände zu viel legen den Bus lahm (äußert sich als Bus-Off).
- Freie GPIOs auf dem C3: 0–10, 20, 21. **Nicht** verwenden: 18/19 (USB),
  11–17 (Flash), 2/8/9 (Strapping).
- Stromversorgung: eigenes 5-V-Netzteil oder Step-down. Nicht gleichzeitig USB eines
  geerdeten Rechners und CAN-Masse anklemmen (Erdungsschleife).

---

## Protokoll

```
ESP32 ── 0x680 ──> ECU (HMUMASTER)
      <─ 0x690 ──
```

- **250 kBit/s**, 11-Bit-IDs, klassisches CAN (kein CAN-FD)
- UDS: `0x22` ReadDataByIdentifier, `0x2E` WriteDataByIdentifier
- Transport: **ISO-TP**. Anfragen passen immer in ein Single Frame (≤ 7 Byte);
  Antworten bis 24 Byte kommen als First Frame + Consecutive Frames.
  Nach dem First Frame sendet der ESP32 einen Flow-Control-Frame
  (`0x30`, Blocksize 0, STmin 0) und sammelt dann alle Frames ein.

### Ablauf

Die Klasse arbeitet **nicht blockierend**: ein 20-ms-Intervall ruft `loop()`, das pro
Aufruf höchstens einen Schritt macht (Senden, Frames einsammeln, auswerten). Dadurch
bleiben WLAN und API reaktionsfähig.

| Parameter | Wert | Bedeutung |
|---|---|---|
| Abfragetakt | 30 s | 7 Messwerte (Temperaturen, Druck, Flamme, Pumpe) |
| Abfragetakt | 300 s | 15 Werte (Statistik, Diagnose, Gateway) |
| Antwort-Timeout | 400 ms | danach gilt der DID als fehlgeschlagen, nächster Versuch in 10 s |
| Pause zwischen Anfragen | 25 ms | schont den Bus |
| Wiederholungen | – | kein Retry in derselben Runde (nur der nächste reguläre Zyklus) |

### Schreibpfad (nur Heizkurve, DID 880)

1. DID 880 vollständig lesen (4 Byte)
2. Byte 0 ersetzen: `round(Steigung × 10)` (0,7 → `0x07`)
3. die **4 Byte** per `0x2E` zurückschreiben
4. auf `0x6E` warten; `0x7F` (Negative Response) wird als Fehler mit NRC gemeldet

Das ist exakt das Read-Modify-Write, das open3e bei Sub-DID-Schreibzugriffen macht.
Maximal 3 Versuche mit 2 s Abstand, danach Abbruch.

---

## Datenpunkte (22)

Kurzfassung mit Live-Werten als Anhang: [`docs/dids-heizung.md`](docs/dids-heizung.md).

| DID | open3e-Name | Byte | Inhalt | Einheit | Zugriff | Takt |
|---:|---|---:|---|---|---|---|
| 268 | `FlowTemperatureSensor` | 9 | 4× int16 /10 (Ist/Min/Max/Mittel) + Status | °C | ro | 30 s |
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
| 880 | `MixerOneCircuitCentralHeatingCurve` | 4 | int8 /10, int8 signed, int16 signed | – | **rw** | 300 s |
| 929 | `GasType` | 1 | Enum (1 LLGas, 2 EGas, 3 Flüssiggas) | – | ro | 300 s |
| 901 | `ServiceManagerIsRequired` | 1 | Byte ≠ 0 = Wartung | – | ro | 300 s |
| 902 | `MalfunctionIdentification` | 1 | Byte ≠ 0 = Störung | – | ro | 300 s |
| 522 | `ServiceDateNext` | 4 | Tag, Monat, 2000+Jahr, Status | Datum | ro | 300 s |
| 265 | `ErrorDtcList` | 122 | uint16 Anzahl + n× 12 Byte (Enum, Zeitstempel, 2× raw) | – | ro | 300 s |
| 1165 | `BackendConnectionStatus` | 1 | 3 = verbunden | – | ro | 300 s |
| 593 | `GatewayMac` | 6 | MAC des Internetmoduls | – | ro | 300 s |
| 607 | `GatewayRemoteIp` | 20 | 5× IPv4, erstes = WLAN-IP | – | ro | 300 s |
| 602 | `GatewayRemoteLocalNetworkStatus` | 1 | 3 = verbunden | – | ro | 300 s |
| 900 | `GatewayRemoteSignalStrength` | 1 | 0–100 | % | ro | 300 s |

### Abweichungen von den open3e-Metadaten

- **DID 318**: open3e deklariert `hPa`, geliefert wird aber der Bar-Wert (1,4).
  Hier als `bar` veröffentlicht – passend zur HA-Integration.
- **DID 880**: open3e führt das Feld als `acc="ro"`, die HA-Integration schreibt es
  aber. Am Gerät wurde bestätigt: der Schreibzugriff wird akzeptiert.
- **DID 593/607/602/900/1165** beschreiben das **eingebaute Viessmann-Internetmodul**
  (MAC-OUI `B8:74:24` = Viessmann Elektronik), nicht dieses Gateway.
- **DID 265**: hier werden nur Anzahl und Fehler-IDs (`0x1234`) ausgegeben, nicht die
  Klartextnamen – dafür müsste die komplette `Errors`-Enum mit auf das Gerät.

---

## Entitäten (54)

**Sensoren (30)** – Vorlauf, Vorlauf-Soll, Außentemperatur, Abgas, Wasserdruck,
Betriebs-/Brennerstunden, Brennerstarts, je 4× Energie/Gas/Wärme (heute, 7 Tage,
Monat, Jahr), Ionisationsstrom, Pumpen-Soll/-Ist, Heizkurve Niveau/Fußpunkt,
Fehleranzahl, Gateway-WLAN-Signal, Datenalter, CAN-Anfragen-OK, CAN-Fehler

**Textsensoren (14)** – Sensorstatus (Vorlauf, Außen, Abgas, Druck), Gastyp,
Servicetermin, Fehlerliste, Gateway-MAC/-IP/-Netzwerkstatus, CAN-Bus-Status,
CAN letzter Fehler, Schreibstatus, Bus-IDs (Diagnose)

**Binärsensoren (6)** – Brennerflamme, Heizkreispumpe, Wartung erforderlich,
Störung vorhanden, Backend verbunden, **CAN Daten aktuell**

**Weitere (4)** – `number` Heizkurve 1 Steigung (0,2–3,5),
`switch` Schreiben freigeben, `button` CAN Bus neu starten,
`button` Bus-Scan starten

### Verhalten bei veralteten Daten

Jeder DID merkt sich den Zeitpunkt der letzten erfolgreichen Antwort. Ist er älter als
5 Abfragezyklen (mindestens 2 Minuten), gilt der Wert als veraltet:

- numerische Sensoren senden **nichts** mehr (der letzte Wert bleibt stehen)
- Textsensoren zeigen `veraltet`
- `CAN Daten aktuell` geht auf *aus*, `Datenalter` zählt die Sekunden seit der letzten
  Antwort

Bewusst **kein** `NAN`: Home Assistant wirft bei Sensoren mit `device_class`/`state_class`
einen `ValueError` („non-finite value") und protokolliert das bei jedem Update.

---

## Installation

### 1. Dateien

| Datei | Ziel |
|---|---|
| `vitodens-can.yaml` | ESPHome-Konfigurationsordner |
| `vitodens-uds.h` | derselbe Ordner (wird per `includes:` eingebunden) |

`secrets.yaml` benötigt: `wifi_ssid`, `wifi_password`, `api_encryption_key`,
`web_password` (letzteres nur, wenn der Webserver aktiviert wird).

### 2. Kompilieren und flashen

```bash
esphome config vitodens-can.yaml     # Schema prüfen
esphome compile vitodens-can.yaml
esphome upload vitodens-can.yaml --device <IP-oder-/dev/ttyUSB0>
```

Oder über den ESPHome Device Builder: Gerät `vitodens-can` → Install.

### 3. Home Assistant

Das Gerät meldet sich per ESPHome-API; nach dem Hinzufügen erscheinen die 52 Entitäten.
Die Namen sind deutsch; die endgültigen Entitäts-IDs vergibt Home Assistant beim
Anlegen (Beispiel: `sensor.vitodens_can_vorlauftemperatur`).

---

## Konfiguration

```yaml
substitutions:
  can_tx_pin: "4"     # CTX des Transceivers
  can_rx_pin: "5"     # CRX des Transceivers
```

Abfragetakte, Timeouts und das Frischefenster stehen als Konstanten am Anfang von
`vitodens-uds.h`:

```cpp
static constexpr uint32_t REQUEST_TIMEOUT_MS = 400;
static constexpr uint32_t GAP_MS            = 25;
static constexpr uint32_t FAST_PERIOD_MS    = 30000;
static constexpr uint32_t SLOW_PERIOD_MS    = 300000;
```

Weitere DIDs ergänzen: Eintrag in `jobs_[]` (Periode wählen) und ein `case` in
`decode_()`.

---

## Sicherheit und Betrieb

1. **Nur ein Tester am Bus.** Vor dem Start `open3e` (Docker/Add-on) stoppen. Zwei
   Tester erzeugen Kollisionen und falsche Werte.
2. **Schreiben ist standardmäßig gesperrt.** Der Schalter *Schreiben freigeben* steht
   nach jedem Neustart auf *aus* (`restore_mode: ALWAYS_OFF`). Erst einschalten, dann
   die Heizkurve ändern. So kann auch ein Sprachassistent nichts verstellen.
3. **Wertebereich** der Heizkurven-Steigung: 0,2–3,5. Die Firmware rundet auf
   Zehntel und schreibt nur Byte 0 des Datenpunkts.
4. Diese Firmware ersetzt **keine** Herstellerwartung und keine Sicherheitsfunktionen
   der Heizung.

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
| `Bus-IDs (Diagnose)` | Ergebnis des Adress-Scans: welche ECU auf DID 256 antwortet |
| Button `Bus-Scan starten` | klopft `0x680..0x6EF` mit DID 256 ab (~20 s), Ergebnis im Log und in `Bus-IDs` |

Häufige Negative Response Codes:

| NRC | Bedeutung | typische Ursache |
|---|---|---|
| `0x22` | conditionsNotCorrect | falsche Betriebsart / Zündung aus |
| `0x31` | requestOutOfRange | DID existiert an dieser ECU nicht |
| `0x33` | securityAccessDenied | Schreibzugriff gesperrt |
| `0x78` | responsePending | Gerät braucht länger (kein Fehler) |

`Request timed out` bei **allen** DIDs deutet auf Verkabelung, Bitrate oder fehlende
Masse – nicht auf die Firmware. Der Bus bleibt dabei still, das Gerät sendet aber
weiter Anfragen und geht nach einiger Zeit in Bus-Off; `check_bus_()` erholt sich
alle 5 s selbst.

---

## Stolperfallen (aus der Entwicklung, alle verifiziert)

1. **Die DID-Wiederholung in einer UDS-Antwort ist big-endian.** Dieser Fehler hat
   am längsten gekostet: `62 01 0C …` für DID 268 wurde mit `le_u16()` als `0x0C01`
   gelesen, passte nie zur angefragten DID und **jede gültige Antwort wurde
   verworfen** – sichtbar nur als Timeout, obwohl ACK und Antwort kamen.
   ```cpp
   be_u16(data + 1) == did   // richtig: (data[1] << 8) | data[2]
   ```
   Nutzdaten *innerhalb* eines Datenpunkts sind dagegen little-endian.
2. **`driver/twai.h` fehlt bei ESPHome 2026.10.** ESPHome schließt die IDF-Treiber aus.
   Beide Komponenten müssen im Framework-Abschnitt wieder aktiviert werden:
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
4. **`NAN` an Home-Assistant-Sensoren ist keine gute Idee** (siehe oben). „Unbekannt"
   lässt sich damit nicht erzeugen – nur HA-Fehler.
5. **Ein Timeout ohne `job_done_` blockiert alles.** Bleibt der Job „überfällig", fragt
   der Scheduler denselben DID im 25-ms-Takt erneut und kommt nie zu den anderen.
   Gleiches gilt für den NRC-Pfad.
6. **Schreibzustand muss bei Abbrüchen mit aufgeräumt werden.** Wird nur der
   Empfangszustand zurückgesetzt (z. B. bei Bus-Recovery), bleibt der Schreibvorgang
   „laufend" und `request_curve()` verweigert dauerhaft.
7. **`total_increasing` nur für monotone Werte.** Bei rollierenden Fenstern
   (7 Tage, Monat, Jahr) interpretiert Home Assistant jeden Rückgang als
   Zählerrücksetzung und bucht Phantomverbrauch. `measurement` ist dort richtig –
   aber nicht zusammen mit `device_class: energy`/`gas` (HA lehnt die Kombination ab).
8. **Diagnose per Adress-Scan.** Wenn auf eine Anfrage nur Timeouts kommen, obwohl
   der Bus ACKs liefert: alle ECU-Adressen `0x680..0x6EF` mit DID 256
   (`BusIdentification`, bei E3-Geräten immer vorhanden) abklopfen. Genau so wurde
   der Big-Endian-Fehler gefunden – und bestätigt, dass `0x680`, `0x684` und `0x688`
   alle antworten. Der Scan läuft nicht beim Boot (kostet 20 s), sondern über den
   Button **Bus-Scan starten**.

---

## Grenzen und offene Punkte

- Ziel-ECU ist `0x680` (Antwort `0x690`). Der Adress-Scan zeigt, dass `0x684` und
  `0x688` ebenfalls antworten, und der Empfangsfilter akzeptiert `0x690..0x69F` –
  angebunden ist aber nur `0x680`. Für weitere ECUs müsste die Adresse pro DID
  konfigurierbar werden.
- Fehlerliste ohne Klartextnamen (siehe DID 265).
- Nur ein Stellwert. Weitere `rw`-DIDs (z. B. 396 Warmwasser-Soll, 537/538
  Betriebsart) sind nicht implementiert.
- Kein Tester-Present, keine Session-Steuerung – bisher nicht nötig, da alle
  Lesezugriffe in der Default-Session funktionieren.
- Keine Langzeit-Statistik für Verbrauchswerte (bewusst `measurement`).

---

## Verifikation

| Prüfung | Ergebnis |
|---|---|
| Byte-Layouts | gegen `Open3Edatapoints.py` abgeglichen |
| C++ | Kompilat mit `-Wall -Wextra` gegen IDF-Stubs fehlerfrei |
| Firmware | mehrfach mit ESPHome 2026.10.1 / IDF 5.5.6 gebaut |
| Laufzeit am Gerät | **bestanden** – 22 DIDs, 0 Fehler, 0 Timeouts |
| Plausibilität | Vorlauf 33,5 °C, Außen 10,9 °C, Abgas 31,9 °C, Wasserdruck 1,4 bar, Betriebsstunden 25.906 h, Brennerstarts 10.002, Flamme an, Pumpe 60 % |
| Gegenprobe open3e | identische Werte für Wasserdruck (1,4), Heizkurve (0,4), Gateway-MAC (`B8-74-24-37-D5-98`), WLAN-IP (`192.168.178.80`), Wartung (an) |
| Schreibbarkeit DID 880 | über die open3e-HA-Integration bestätigt (0,7 gesetzt, zurückgelesen, gehalten) |
| Schreiben mit dieser Firmware | **bestanden** – Rundlauf 0,4 → 0,5 → 0,4: jeweils `0x6E` und korrekt zurückgelesen, 0 CAN-Fehler. Nach dem Schreiben wird DID 880 nach 2 s erneut gelesen (Schreibbestätigung). |

---

## Lizenz

Apache License 2.0 – siehe [`LICENSE`](LICENSE) und [`NOTICE`](NOTICE).
Copyright 2026 acki1989.

Die Datenpunktdefinitionen (DID-Nummern, Byte-Layouts, Skalierungsfaktoren,
Enum-Werte) stammen aus [open3e](https://github.com/open3e/open3e) (Apache-2.0).
Es wurde kein Quellcode von open3e übernommen.

## Haftung

Code und Dokumentation ohne Gewähr. Die Benutzung erfolgt auf eigenes Risiko,
insbesondere Schreibzugriffe auf eine Heizungsanlage. Keine Verbindung zu
Viessmann oder zum open3e-Projekt.
