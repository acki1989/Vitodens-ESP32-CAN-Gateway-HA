#!/usr/bin/env python3
"""
Gehaeuse fuer ESP32-C3 SuperMini + SN65HVD230 CAN-Modul (3,3 V).
Erzeugt Boden und Deckel als STL.

Bedarf: python3, trimesh, shapely, manifold3d, numpy
Aufruf: python3 gehaeuse-generator.py [zielordner]

Alle Masse in Millimetern. Die Parameter unten sind die einzigen Stellen,
die zum Anpassen geaendert werden muessen.
"""
import os
import sys

import trimesh
from shapely.geometry import box as shp_box

# ----------------------------------------------------------------- Parameter
FLOOR_T      = 3.0    # Bodenplatte
WALL         = 2.0    # Wandstaerke
INNER_H      = 15.0   # Innenhoehe ueber dem Boden
CORNER_OUT   = 4.0    # Aussenradius
CORNER_IN    = 1.0    # Innenradius
CLEAR        = 0.5    # Luft je Seite in den Board-Taschen
POCKET_D     = 1.7    # Tiefe der Board-Taschen
LID_T        = 2.0    # Deckelplatte
RIM_T        = 1.8    # Deckel-Rand
RIM_H        = 2.2    # Hoehe des Deckel-Rands
RIM_GAP      = 0.25   # Luft je Seite zwischen Rand und Innenwand

# CAN-Modul SN65HVD230, kompakte Bauform (28 x 11 mm)
CAN_L, CAN_W = 28.0, 11.0
TERM_W, TERM_H, TERM_D = 10.4, 10.2, 10.0   # blaue 2-polige Schraubklemme
PIN_LEN = 10.5                              # Stiftleiste ab Platinenende

# ESP32-C3 SuperMini (22,5 x 18 mm)
ESP_L, ESP_W = 22.5, 18.0
USB_W, USB_H = 10.6, 4.2                    # Groesse der USB-Aussparung

BAY_GAP_FRONT = 0.2   # Tasche zur Innenwand vorne

# ------------------------------------------------------- abgeleitete Masse
CAN_POCKET_L = CAN_L + PIN_LEN + 2 * CLEAR
CAN_POCKET_W = CAN_W + 2 * CLEAR
ESP_POCKET_L = ESP_L + 2 * CLEAR
ESP_POCKET_W = ESP_W + 2 * CLEAR

# X = Tiefe (Front bei 0)
X_IN = WALL
CAN_POCKET_X0 = X_IN + BAY_GAP_FRONT
CAN_POCKET_X1 = CAN_POCKET_X0 + CAN_POCKET_L
OUT_X = CAN_POCKET_X1 + 3.0 + WALL

ESP_POCKET_X0 = X_IN + BAY_GAP_FRONT
ESP_POCKET_X1 = ESP_POCKET_X0 + ESP_POCKET_L

# Y = Breite
CAN_BAY_W = CAN_POCKET_W + 0.2
ESP_BAY_W = ESP_POCKET_W + 1.0
CAN_Y0 = WALL
CAN_Y1 = CAN_Y0 + CAN_BAY_W
DIV_Y0 = CAN_Y1
DIV_Y1 = DIV_Y0 + 2.0
ESP_Y0 = DIV_Y1
ESP_Y1 = ESP_Y0 + ESP_BAY_W
OUT_Y = ESP_Y1 + WALL

OUT_Z = FLOOR_T + INNER_H
CAN_BAY_CY = (CAN_Y0 + CAN_Y1) / 2
ESP_BAY_CY = (ESP_Y0 + ESP_Y1) / 2

PCB_TOP = FLOOR_T - POCKET_D + 1.6          # Oberkante Platine in der Tasche
TERM_TOP = PCB_TOP + TERM_H


def rrect(w, d, r, cx=0.0, cy=0.0):
    """Abgerundetes Rechteck als Shapely-Polygon."""
    p = shp_box(cx - w / 2, cy - d / 2, cx + w / 2, cy + d / 2)
    return p.buffer(-r, join_style=1, quad_segs=8).buffer(r, join_style=1, quad_segs=8)


def box(x0, x1, y0, y1, z0, z1):
    m = trimesh.creation.box(extents=(x1 - x0, y1 - y0, z1 - z0))
    m.apply_translation(((x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2))
    return m


def prism(poly, z0, z1):
    m = trimesh.creation.extrude_polygon(poly, height=(z1 - z0))
    m.apply_translation((0, 0, z0))
    return m


def boolean(meshes, op):
    return getattr(trimesh.boolean, op)(meshes, engine="manifold")


# ------------------------------------------------------------------- Boden
outer = rrect(OUT_X, OUT_Y, CORNER_OUT, OUT_X / 2, OUT_Y / 2)
inner = rrect(OUT_X - 2 * WALL, OUT_Y - 2 * WALL, CORNER_IN, OUT_X / 2, OUT_Y / 2)

base = prism(outer, 0, OUT_Z)
base = boolean([base, prism(inner, FLOOR_T, OUT_Z + 1)], "difference")

cuts = [
    # Board-Taschen
    box(CAN_POCKET_X0, CAN_POCKET_X1,
        CAN_Y0 + (CAN_BAY_W - CAN_POCKET_W) / 2,
        CAN_Y1 - (CAN_BAY_W - CAN_POCKET_W) / 2,
        FLOOR_T - POCKET_D, FLOOR_T + 0.01),
    box(ESP_POCKET_X0, ESP_POCKET_X1,
        ESP_Y0 + (ESP_BAY_W - ESP_POCKET_W) / 2,
        ESP_Y1 - (ESP_BAY_W - ESP_POCKET_W) / 2,
        FLOOR_T - POCKET_D, FLOOR_T + 0.01),
    # Frontoeffnung CAN: Klemme und Kabel
    box(-1, WALL + 0.2, CAN_Y0, CAN_Y1, FLOOR_T, TERM_TOP + 1.0),
    # Frontoeffnung USB-C
    box(-1, WALL + 0.2,
        ESP_BAY_CY - USB_W / 2, ESP_BAY_CY + USB_W / 2,
        PCB_TOP - 0.3, PCB_TOP + USB_H),
]
base = boolean([base] + cuts, "difference")
base = boolean([base, box(WALL, OUT_X - WALL, DIV_Y0, DIV_Y1,
                          FLOOR_T, FLOOR_T + 8.0)], "union")

# ------------------------------------------------------------------ Deckel
lid = prism(outer, 0, LID_T)
rim_outer = rrect(OUT_X - 2 * WALL - 2 * RIM_GAP,
                  OUT_Y - 2 * WALL - 2 * RIM_GAP, CORNER_IN - 0.2,
                  OUT_X / 2, OUT_Y / 2)
rim_inner = rrect(OUT_X - 2 * WALL - 2 * RIM_GAP - 2 * RIM_T,
                  OUT_Y - 2 * WALL - 2 * RIM_GAP - 2 * RIM_T,
                  max(CORNER_IN - RIM_T, 0.4), OUT_X / 2, OUT_Y / 2)
rim = prism(rim_outer, LID_T, LID_T + RIM_H)
rim = boolean([rim, prism(rim_inner, LID_T - 0.1, LID_T + RIM_H + 1)],
              "difference")
lid = boolean([lid, rim], "union")

# Lueftungsschlitze
vents = []
for y in (CAN_BAY_CY - 3.2, CAN_BAY_CY, CAN_BAY_CY + 3.2,
          ESP_BAY_CY - 4.5, ESP_BAY_CY - 1.5, ESP_BAY_CY + 1.5,
          ESP_BAY_CY + 4.5):
    vents.append(box(8.0, 22.0, y - 1.0, y + 1.0, -1, LID_T + 0.1))
lid = boolean([lid] + vents, "difference")

# Griffkerbe an der Rueckkante
lid = boolean([lid, box(20.0, 27.0, OUT_Y - 1.3, OUT_Y + 1, 0.9, LID_T + 0.1)],
              "difference")

# ------------------------------------------------------------------ Export
out_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
os.makedirs(out_dir, exist_ok=True)
for name, mesh in (("ESP32-C3_CAN-Gehaeuse-Boden", base),
                   ("ESP32-C3_CAN-Gehaeuse-Deckel", lid)):
    mesh.apply_translation(-mesh.bounds[0])
    path = os.path.join(out_dir, name + ".stl")
    mesh.export(path)
    print(f"{path}: {mesh.extents.round(2).tolist()} mm, "
          f"{mesh.volume / 1000:.2f} cm³, wasserdicht={mesh.is_watertight}")
