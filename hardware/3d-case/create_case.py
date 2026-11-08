#!/usr/bin/env python3
"""SolarHeatingCase: contenitore parametrico per NodeMCU ESP8266.

Genera due parti STEP e STL per SolidWorks e stampa 3D.
Tutte le misure sono in millimetri. Richiede CadQuery 2.x.
ATTENZIONE: verificare le misure reali prima della stampa definitiva.
"""

from pathlib import Path
import cadquery as cq
from cadquery import exporters

# ---------------------------------------------------------------------------
# 1. PARAMETRI PRINCIPALI
# ---------------------------------------------------------------------------
LENGTH = 110.0
WIDTH = 78.0
BASE_HEIGHT = 26.0
LID_THICKNESS = 3.2
FLOOR_THICKNESS = 2.8
WALL_THICKNESS = 2.4
CORNER_RADIUS = 6.0

BOSS_OFFSET_X = 47.4
BOSS_OFFSET_Y = 31.0
BOSS_DIAMETER = 12.0
BOSS_PILOT_DIAMETER = 2.6
LID_CLEARANCE_DIAMETER = 3.4
LID_COUNTERBORE_DIAMETER = 6.2

# Il fissaggio ESP8266 usa fascette; non assume dimensioni/fori PCB identici.
ESP_PCB_ESTIMATED_LENGTH = 60.0
ESP_PCB_ESTIMATED_WIDTH = 32.0
OUTPUT_DIR = Path(__file__).resolve().parent


def rounded_prism(length, width, height, radius, x=0.0, y=0.0, z=0.0):
    """Crea un prisma con quattro angoli verticali arrotondati."""
    solid = cq.Workplane("XY").box(
        length, width, height, centered=(True, True, False)
    )
    solid = solid.edges("|Z").fillet(radius)
    return solid.translate((x, y, z))


def box(length, width, height, x=0.0, y=0.0, z=0.0):
    """Crea un prisma con base sul piano z specificato."""
    solid = cq.Workplane("XY").box(
        length, width, height, centered=(True, True, False)
    )
    return solid.translate((x, y, z))


def vertical_hole(radius, height, x, y, z):
    """Cilindro lungo Z per fori e scassi."""
    solid = cq.Workplane("XY").circle(radius).extrude(height)
    return solid.translate((x, y, z))


def make_base():
    """Vasca principale con fissaggi, feritoie e aperture funzionali."""
    outer = rounded_prism(LENGTH, WIDTH, BASE_HEIGHT, CORNER_RADIUS)
    inner = rounded_prism(
        LENGTH - 2 * WALL_THICKNESS,
        WIDTH - 2 * WALL_THICKNESS,
        BASE_HEIGHT + 2.0,
        CORNER_RADIUS - WALL_THICKNESS,
        z=FLOOR_THICKNESS,
    )
    base = outer.cut(inner)

    # Colonnine per le quattro viti di chiusura.
    for x in (-BOSS_OFFSET_X, BOSS_OFFSET_X):
        for y in (-BOSS_OFFSET_Y, BOSS_OFFSET_Y):
            boss = vertical_hole(
                BOSS_DIAMETER / 2, BASE_HEIGHT - FLOOR_THICKNESS,
                x, y, FLOOR_THICKNESS
            )
            base = base.union(boss)
            bore = vertical_hole(
                BOSS_PILOT_DIAMETER / 2, 14.0, x, y, BASE_HEIGHT - 14.0
            )
            base = base.cut(bore)

    # Appoggi della scheda principale, da fissare con fascette.
    for x in (-43.0, -24.0, 1.0):
        for y in (-10.5, 10.5):
            base = base.union(
                box(7.0, 7.0, 3.5, x=x, y=y, z=FLOOR_THICKNESS)
            )

    # Due coppie di asole per fascette larghe fino a circa 2,5 mm.
    for x in (-20.0, 0.0):
        for y in (-19.0, 19.0):
            base = base.cut(
                box(3.3, 5.6, FLOOR_THICKNESS + 1.0,
                    x=x, y=y, z=-0.5)
            )

    # Piano IR per KY-005 e relativo foro di trasmissione verso l'Hisense.
    base = base.union(
        box(30.0, 11.0, 3.6, x=-16.0, y=23.0, z=FLOOR_THICKNESS)
    )
    for x in (-26.0, -6.0):
        base = base.cut(
            box(3.2, 5.0, FLOOR_THICKNESS + 4.5,
                x=x, y=23.0, z=-0.5)
        )
    ir_hole = cq.Solid.makeCylinder(
        3.4, 9.0,
        cq.Vector(-16.0, WIDTH / 2 - 4.5, 14.0),
        cq.Vector(0, 1, 0)
    )
    base = base.cut(ir_hole)

    # Supporto separato e ventilazione del sensore SHT31.
    base = base.union(
        box(22.0, 22.0, 3.0, x=38.5, y=0.0, z=FLOOR_THICKNESS)
    )
    base = base.union(
        box(1.7, 48.0, 11.0, x=24.0, y=3.5, z=FLOOR_THICKNESS)
    )
    for y in (-17.0, -8.5, 0.0, 8.5, 17.0):
        for z in (10.0, 18.0):
            base = base.cut(
                box(8.0, 4.0, 2.2, x=LENGTH / 2 - 1.0,
                    y=y, z=z)
            )

    # Accesso laterale per micro-USB del NodeMCU.
    base = base.cut(
        box(8.0, 16.0, 11.0,
            x=-LENGTH / 2 + 1.5, y=0.0, z=5.5)
    )

    # Orecchie esterne per montaggio a muro (fori M4).
    for x in (-31.0, 31.0):
        tab = rounded_prism(
            14.0, 13.0, FLOOR_THICKNESS, radius=3.0,
            x=x, y=-43.5
        )
        base = base.union(tab)
        base = base.cut(
            vertical_hole(2.2, FLOOR_THICKNESS + 1.0,
                          x, -45.5, z=-0.5)
        )

    return base


def make_lid():
    """Coperchio con inviti di centraggio e ventilazione del SHT31."""
    lid = rounded_prism(
        LENGTH, WIDTH, LID_THICKNESS,
        CORNER_RADIUS, z=BASE_HEIGHT
    )

    # Linguette interne contro il disallineamento.
    lip_z = BASE_HEIGHT - 1.6
    lip_wall_pos_y = WIDTH / 2 - WALL_THICKNESS - 0.9
    lip_wall_pos_x = LENGTH / 2 - WALL_THICKNESS - 0.9

    for y in (-lip_wall_pos_y, lip_wall_pos_y):
        lid = lid.union(box(65.0, 1.4, 1.65, y=y, z=lip_z))
    for x in (-lip_wall_pos_x, lip_wall_pos_x):
        lid = lid.union(box(1.4, 43.0, 1.65, x=x, z=lip_z))

    # Fori passanti M3 e lamature per le teste delle viti.
    for x in (-BOSS_OFFSET_X, BOSS_OFFSET_X):
        for y in (-BOSS_OFFSET_Y, BOSS_OFFSET_Y):
            lid = lid.cut(
                vertical_hole(
                    LID_CLEARANCE_DIAMETER / 2,
                    LID_THICKNESS + 1.0,
                    x, y, BASE_HEIGHT - 0.5
                )
            )
            lid = lid.cut(
                vertical_hole(
                    LID_COUNTERBORE_DIAMETER / 2,
                    1.2, x, y,
                    BASE_HEIGHT + LID_THICKNESS - 1.2
                )
            )

    # Griglia di ventilazione sopra al sensore.
    for y in (-13.0, -8.0, -3.0, 2.0, 7.0, 12.0):
        lid = lid.cut(
            box(14.0, 2.0, LID_THICKNESS + 1.0,
                x=38.0, y=y, z=BASE_HEIGHT - 0.5)
        )

    return lid


def export_models():
    """Esporta geometrie STEP CAD e mesh STL per slicer."""
    base = make_base()
    lid = make_lid()

    exporters.export(base, str(OUTPUT_DIR / "SolarHeatingCase_BASE.step"))
    exporters.export(lid, str(OUTPUT_DIR / "SolarHeatingCase_LID.step"))

    # Nel file assemblato le parti conservano la posizione relativa.
    assembly = cq.Assembly(name="SolarHeatingCase_ESP8266")
    assembly.add(base, name="Base", color=cq.Color(0.2, 0.4, 0.5))
    assembly.add(lid, name="Coperchio", color=cq.Color(0.85, 0.9, 0.9))
    assembly.save(str(OUTPUT_DIR / "SolarHeatingCase_ASSEMBLY.step"))

    # Base già orientata sul piano di stampa.
    exporters.export(
        base, str(OUTPUT_DIR / "SolarHeatingCase_BASE.stl"),
        tolerance=0.08, angularTolerance=0.12
    )

    # Coperchio rovesciato per stampa senza supporti.
    lid_for_print = lid.rotate((0, 0, 0), (1, 0, 0), 180)
    lid_for_print = lid_for_print.translate(
        (0, 0, BASE_HEIGHT + LID_THICKNESS)
    )
    exporters.export(
        lid_for_print, str(OUTPUT_DIR / "SolarHeatingCase_LID.stl"),
        tolerance=0.08, angularTolerance=0.12
    )

    # Interrompe il processo di build in caso di solido non valido.
    for label, shape in (("BASE", base), ("LID", lid)):
        solid = shape.val()
        bounds = solid.BoundingBox()
        print(
            f"{label}: valid={solid.isValid()} "
            f"solids={len(shape.solids().vals())} "
            f"volume={solid.Volume():.0f} mm3 "
            f"bbox=({bounds.xlen:.1f}, {bounds.ylen:.1f}, "
            f"{bounds.zlen:.1f}) mm"
        )
        if not solid.isValid():
            raise RuntimeError(f"Geometria {label} non valida")


if __name__ == "__main__":
    export_models()
