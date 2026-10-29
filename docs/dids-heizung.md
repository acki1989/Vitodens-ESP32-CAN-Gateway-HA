# Vitodens – DIDs für ESP32-Gateway (nur Heizung, ohne Warmwasser)

Stand: 2026-10-29

Quelle: aktive Entitäten der HA-Integration `open3e` (Gerät `Vitodens-7470457349032224`,
Device-ID `5343de80977cfa4340ac0254f3dcaec0`), abgeglichen mit der Datenpunktliste
aus `open3e/src/open3e/Open3Edatapoints.py`. Werte in der Spalte "Wert" sind die
Live-Stände vom Erstellungszeitpunkt.

Anlage nutzt **kein Warmwasser** – nur Heizkreis. Alle WW-Datenpunkte sind bewusst
weggelassen (siehe Abschnitt "Entfällt").

## Heizung – Messwerte (lesen)

| DID | open3e-Name | Entität | Wert |
|---:|---|---|---|
| 268 | `FlowTemperatureSensor` | Vorlauftemperatur | 42.6 |
| 527 | `FlowTemperatureTargetSetpoint` | Vorlauftemperatur Soll | 15.0 |
| 274 | `OutsideTemperatureSensor` | Außentemperatur | 10.1 |
| 331 | `FlueGasTemperatureSensor` | Abgastemperatur | 39.4 |
| 318 | `WaterPressureSensor` | Wasserdruck | 1.4 |
| 1346 | `HeatEngineStatistical` | Betriebsstunden / Brennerstunden | 25836 / 9809 |
| 548 | `EnergyConsumptionCentralHeating` | Verbrauch Heizung heute | 0.3 |
| 544 | `GasConsumptionCentralHeating` | Gasverbrauch Heizung heute | 1.8 |
| 1211 | `GeneratedCentralHeatingOutput` | Wärmeleistung Heizung heute | 18.0 |
| 929 | `GasType` | Gastyp | `EGas` |

## Schaltzustände (lesen)

| DID | open3e-Name | Entität | Wert |
|---:|---|---|---|
| 364 | `Flame` | Brennerflamme | off |
| 381 | `CentralHeatingPump` | Heizkreispumpe (State + Soll-%) | off |

## Stellwert (schreiben)

| DID | open3e-Name | Entität | Wert |
|---:|---|---|---|
| 880 | `MixerOneCircuitCentralHeatingCurve` | Heizkurve 1 Steigung | 0.4 |

## Diagnose (lesen)

| DID | open3e-Name | Entität | Wert |
|---:|---|---|---|
| 902 | `MalfunctionIdentification` | Fehlererkennung | 0 |
| 265 | `ErrorDtcList` | Fehlerliste | `-` |
| 901 | `ServiceManagerIsRequired` | Wartung erforderlich | True |
| 522 | `ServiceDateNext` | Nächster Servicetermin | 10.09.2026 |
| 1165 | `BackendConnectionStatus` | Backend-/Cloud-Verbindung | connected |
| 593 | `GatewayMac` | MAC des Internetmoduls | B8:74:24:37:D5:98 |
| 607 | `GatewayRemoteIp` | WLAN-IP des Moduls | 192.168.178.80 |
| 602 | `GatewayRemoteLocalNetworkStatus` | WLAN-Status | connected |
| 900 | `GatewayRemoteSignalStrength` | WLAN-Signalstärke | 37 |

**Summe: 22 DIDs** – 10 Messwerte + 2 Zustände + 1 Stellwert + 9 Diagnose.

Hinweis: Die DIDs 593/607/602/900/1165 beschreiben das **eingebaute Viessmann-
Internetmodul** (MAC-OUI `B8:74:24` = Viessmann Elektronik), nicht das open3e-Gateway.
Sie sind über CAN lesbar, für die Heizungsregelung aber nicht erforderlich.

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
| 396 | `DomesticHotWaterTemperatureSetpoint` | WW-Sollwert |
| 360 | `DomesticHotWaterOutletSensor` | WW-Auslauf |

Die früheren HA-Entitäten `…_central_total_today`, `gas_consumption_total_today`
und `generated_heating_water_output_total_today` sind in der Integration **Summen
aus Heizung + Warmwasser**. Bei abgeschaltetem WW genügt jeweils der Heizungsanteil
(548, 544, 1211) – die Messwerte waren zum Erstellungszeitpunkt identisch.

## Umsetzungshinweise

- Bus: E3-CAN, **250 kBit/s**, UDS über ISO-TP (Anfrage an `0x680`, Antwort `0x690`).
- DIDs `268, 274, 318, 331, 1346` liefern 9 Byte → ISO-TP-Segmentierung mit
  Flow-Control nötig.
- `364` (1 Byte), `381` (4 Byte) und `544/548/1211/929/901/902/522` sind kurz,
  `265` ist eine Liste, `880` ist 9 Byte und der einzige Schreibzugriff.
- Nur **ein** aktiver Teilnehmer darf Anfragen auf den Bus stellen – nicht parallel
  zu einem laufenden open3e.
- Optional ergänzbar: `269` Rücklauf, `284` Mischer-Vorlauf, `334` Raumtemperatur.
