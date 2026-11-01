# Vitodens E3 – Datenpunkte des Gateways (nur Heizung, ohne Warmwasser)

Stand: **2026-11-01**, am Gerät verifiziert.

Quelle der Byte-Layouts: `open3e/src/open3e/Open3Edatapoints.py`. Abgeglichen mit den
aktiven Entitäten der HA-Integration `open3e` (Gerät `Vitodens-7470457349032224`) und
mit den Live-Werten des ESP32-Gateways. Die Spalte „Beispiel" ist ein Messwert vom
01.11.2026 – sie belegt, dass der Datenpunkt antwortet, keine Momentaufnahme.

Anlage nutzt **kein Warmwasser** – nur Heizkreis. Alle WW-Datenpunkte sind bewusst
weggelassen (siehe „Entfällt").

## Adressierung auf dem Bus

| | |
|---|---|
| Bus | E3-CAN, **250 kBit/s**, klassisches CAN |
| Anfrage | `0x680` |
| Antwort | `0x690` (ECU-Adresse + `0x10`) |
| Weitere erreichbare ECUs | `0x684` (Antwort `0x694`), `0x688` (Antwort `0x698`) – per Adress-Scan mit DID 256 bestätigt |
| Empfangsfilter | `0x690…0x69F` |
| Transport | ISO-TP, UDS `0x22` (lesen) / `0x2E` (schreiben) |

**Endianness – die häufigste Fehlerquelle:** Die DID-Wiederholung in der Antwort ist
**big-endian** (`62 01 0C …` für DID 268), die Nutzdaten *innerhalb* eines
Datenpunkts sind **little-endian**.

## Heizung – Messwerte (lesen)

| DID | open3e-Name | Byte | Inhalt | Einheit | Beispiel |
|---:|---|---:|---|---|---|
| 268 | `FlowTemperatureSensor` | 9 | 4× int16 /10 (Ist, Min, Max, Mittel) + Status | °C | 45,0 |
| 527 | `FlowTemperatureTargetSetpoint` | 2 | int16 /10 | °C | 15,0 |
| 274 | `OutsideTemperatureSensor` | 9 | wie 268 | °C | 9,4 |
| 331 | `FlueGasTemperatureSensor` | 9 | wie 268 | °C | 42,6 |
| 318 | `WaterPressureSensor` | 9 | wie 268 | bar | 1,4 |
| 1346 | `HeatEngineStatistical` | 12 | 3× int32: Betriebsstunden, Brennerstunden, Starts | h / h / – | 25.906 / 9.809 / 10.002 |
| 526 | `ModulationCurrentValue` | 2 | int16 /10 | % | 0,0 (Brenner aus) |
| 548 | `EnergyConsumptionCentralHeating` | 24 | 6× int32 /10: heute, 7 Tage, Monat, Vormonat, Jahr, Vorjahr | kWh | 0,3 |
| 544 | `GasConsumptionCentralHeating` | 12 | 6× int16 /10, gleiche Staffelung | m³ | 4,9 |
| 1211 | `GeneratedCentralHeatingOutput` | 24 | 6× int32 /10, gleiche Staffelung | kWh | 49,0 |
| 929 | `GasType` | 1 | Enum: 1 LLGas, 2 EGas, 3 Flüssiggas | – | `EGas` |

## Schaltzustände (lesen)

| DID | open3e-Name | Byte | Inhalt | Beispiel |
|---:|---|---:|---|---|
| 364 | `Flame` | 6 | State, 2× raw, int16 /100, raw | Flamme an |
| 381 | `CentralHeatingPump` | 4 | State, Soll-%, Ist, raw | 60 % / 60 % |

Der Ionisationsstrom steckt in DID 364 (Byte 3–4, /100) und wird als eigenes
Diagnose-Entity veröffentlicht (19,8 µA bei brennendem Brenner).

## Stellwert (schreiben)

| DID | open3e-Name | Byte | Inhalt | Zugriff |
|---:|---|---:|---|---|
| 880 | `MixerOneCircuitCentralHeatingCurve` | 4 | Byte 0: Steigung /10 (0,2…3,5) · Byte 1: Niveau signed, 1 K (−13…40) · Byte 2–3: Fußpunkt int16 **/10** | **rw** (Steigung, Niveau) |

**Schreibverfahren:** DID 880 vollständig lesen → **ein** Byte ersetzen (0 = Steigung
`round(Steigung × 10)`, 1 = Niveau `round(Niveau)`) → alle 4 Byte per `0x2E`
zurückschreiben. Der Fußpunkt (Byte 2–3) ist nur lesbar. Genau das macht
open3e bei Sub-DID-Schreibzugriffen auch. open3e führt das Feld als `acc="ro"`,
die ECU akzeptiert den Schreibzugriff aber – 2026-11-01 mit einem Rundlauf
0,4 → 0,5 → 0,4 für die Steigung und mit unverändertem Wert für das Niveau bestätigt
(jeweils `0x6E`, Wert korrekt zurückgelesen).

## Diagnose (lesen)

| DID | open3e-Name | Byte | Inhalt | Beispiel |
|---:|---|---:|---|---|
| 902 | `MalfunctionIdentification` | 1 | Byte ≠ 0 = Störung | 0 |
| 265 | `ErrorDtcList` | 122 | uint16 Anzahl + n× 12 Byte (Enum, Zeitstempel, 2× raw) | `keine` |
| 901 | `ServiceManagerIsRequired` | 1 | Byte ≠ 0 = Wartung fällig | 1 |
| 522 | `ServiceDateNext` | 4 | Tag, Monat, 2000+Jahr, Status | 10.09.2026 |
| 1165 | `BackendConnectionStatus` | 1 | 3 = verbunden | verbunden |
| 593 | `GatewayMac` | 6 | MAC des Internetmoduls | B8-74-24-37-D5-98 |
| 607 | `GatewayRemoteIp` | 20 | 5× IPv4, erstes = WLAN-IP | 192.168.178.80 |
| 602 | `GatewayRemoteLocalNetworkStatus` | 1 | 3 = verbunden | verbunden |
| 900 | `GatewayRemoteSignalStrength` | 1 | 0–100 | 41 |

**Summe: 23 DIDs** – 11 Messwerte + 2 Zustände + 1 Stellwert + 9 Diagnose.

Hinweis: 593/607/602/900/1165 beschreiben das **eingebaute Viessmann-Internetmodul**
(MAC-OUI `B8:74:24` = Viessmann Elektronik), nicht das Gateway. Über CAN lesbar, für
die Heizungsregelung aber entbehrlich.

## Entfällt (kein Warmwasser)

| DID | open3e-Name | Grund |
|---:|---|---|
| 271 | `DomesticHotWaterSensor` | Warmwassertemperatur |
| 531 | `DomesticHotWaterOperationState` | WW-Betriebsstatus |
| 491 | `DomesticHotWaterCirculationPump` | WW-Umwälzpumpe |
| 497 | `DomesticHotWaterCirculationPumpMode` | Legionellenschutz / WW-Pumpe |
| 565 | `EnergyConsumptionDomesticHotWater` | Verbrauch WW |
| 545 | `GasConsumptionDomesticHotWater` | Gasverbrauch WW |
| 1391 | `GeneratedDomesticHotWaterOutput` | Wärmeleistung WW |
| 396 | `DomesticHotWaterTemperatureSetpoint` | WW-Sollwert (`rw`) |
| 360 | `DomesticHotWaterOutletSensor` | WW-Auslauf |

Die Entitäten `…_central_total_today`, `gas_consumption_total_today` und
`generated_heating_water_output_total_today` der Integration sind **Summen aus
Heizung + Warmwasser**. Bei abgeschaltetem WW genügt jeweils der Heizungsanteil
(548, 544, 1211).

## Umsetzungshinweise

- **Ein Tester genügt nicht** – es darf immer nur *ein* Teilnehmer Anfragen stellen.
  Nicht parallel zu einem laufenden open3e.
- **Antworten > 7 Byte** brauchen ISO-TP-Segmentierung: First Frame + Flow-Control
  (Blocksize 0, STmin 0) + Consecutive Frames. Betrifft 268, 274, 318, 331 (9 Byte),
  1346 (12), 544 (12), 548 und 1211 (24) sowie 265 (bis 122).
- **Kurze Antworten** (1–6 Byte) kommen als Single Frame: 527, 364, 381, 880, 929,
  901, 902, 522, 1165, 593, 607, 602, 900.
- **Zwei Stellwerte** auf DID 880: Steigung (Byte 0) und Niveau (Byte 1).
  Der Fußpunkt wird gelesen, aber nicht geschrieben. Weitere `rw`-Datenpunkte wären z. B.
  396 (WW-Soll), 537/538 (externe Betriebsart) – nicht enthalten.
- **Fehlerliste ohne Klartext:** DID 265 liefert Fehler-IDs (`0x1234`), keine Namen.
  Dafür müsste die komplette `Errors`-Enum mit auf das Gerät.
- Optional ergänzbar: `269` Rücklauf, `284` Mischer-Vorlauf, `334` Raumtemperatur.
