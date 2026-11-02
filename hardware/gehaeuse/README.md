# Gehäuse für ESP32-C3 SuperMini + SN65HVD230 (CAN)

Druckvorlage für ein kleines Gehäuse, in dem beide Platinen dieses Projekts
nebeneinander Platz haben: das ESP32-C3-SuperMini-Board und das
SN65HVD230-CAN-Modul (3,3 V, kompakte Bauform mit 4-Pin-Stiftleiste und
blauer 2-poliger Schraubklemme).

Zurück zur Projekt-Doku: [`../../README.md`](../../README.md)

## Dateien

| Datei | Inhalt |
|---|---|
| `ESP32-C3_CAN-Gehaeuse-Boden.stl` | Unterteil mit Board-Taschen, Trennwand, Ausschnitten |
| `ESP32-C3_CAN-Gehaeuse-Deckel.stl` | Deckel mit Rand, Lüftungsschlitzen, Griffkerbe |
| `Vorschau.png` | montiertes Gehäuse (Render) |
| `gehaeuse-generator.py` | parametrisches Quellskript (optional, zum Anpassen) |

## Maße

* außen: **46,7 × 38,2 mm**, Boden 18 mm + Deckel 2 mm = **20 mm** hoch
* innen: 42,7 × 34,2 × 15 mm
* Board-Taschen (1,7 mm tief):
  * CAN-Modul: **39,5 × 12,0 mm** – Platine 28 mm + 10,5 mm Stiftleiste
  * ESP32: **23,5 × 19,0 mm**
* Frontöffnung CAN (Klemme + Kabel): 12,2 × 11,1 mm
* USB-C-Öffnung: 10,6 × 4,2 mm

Die Taschen haben 0,5 mm Luft je Seite, der Deckel sitzt mit 0,25 mm Luft
je Seite als Presssitz.

## Drucken

* 0,2 mm Schichthöhe, 3 Wände, 15–20 % Infill
* **keine Stützen nötig**
* Boden: Unterseite flach aufs Druckbett
* Deckel: Platte nach unten, Rand zeigt nach oben (so ist die STL ausgerichtet)
* PLA oder PETG; PETG sitzt etwas satter

## Montage

1. **ESP32 vorbereiten:** Die Tasche ist für die nackte Platine (1,6 mm)
   ausgelegt. Vorhandene Stiftleisten entfernen oder Kabel direkt an die
   Pads löten. Der USB-C-Anschluss liegt dann in der Frontöffnung.
2. **CAN-Modul** mit der Schraubklemme zur offenen Front in die lange Tasche
   legen. Die vier Stiftpins liegen hinten in der Tasche.
3. **Verdrahten** (3V3, GND, CAN-TX/CAN-RX) – am besten vor dem Schließen,
   die Kabel lassen sich nach oben herausführen.
4. **Deckel** aufdrücken. Zum Öffnen die Kerbe an der Rückkante nutzen.

## Hinweise

* Die Maße stammen aus Produktfotos und den Produktangaben
  (CAN-Modul 28 × 11 mm). Rechne mit ±0,3 mm Fertigungstoleranz.
* Ist das CAN-Modul die lange Variante (ca. 40 × 17 mm), passen die Taschen
  nicht – dann die Parameter `CAN_L`/`CAN_W` im Skript anpassen.
* Die Lüftungsschlitze liegen über beiden Platinen; der ESP32-C3 wird kaum
  warm, die Schlitze sind eher für die Antenne und gegen Staunässe gedacht.

## Selbst anpassen

`gehaeuse-generator.py` erzeugt beide STLs neu. Benötigt Python 3 mit
`trimesh`, `shapely`, `manifold3d` und `numpy`:

```bash
python3 gehaeuse-generator.py .
```

Die wichtigsten Stellschrauben stehen ganz oben im Skript:
`CLEAR` (Luft in den Taschen), `POCKET_D` (Tiefe), `INNER_H` (Innenhöhe),
`CAN_L/CAN_W`, `ESP_L/ESP_W` sowie `RIM_GAP` für den Deckelsitz.
