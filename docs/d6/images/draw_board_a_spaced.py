"""Board A's spaced 65-row layout. JY V2 rows 4-9 remain unchanged."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

out = Path(__file__).resolve().parent / "board-a-spaced.png"
W, H = 1800, 1650
bg, ink, muted = "#f5f7fb", "#18304d", "#415a77"
green, blue, orange, purple, red = "#237c3e", "#177db9", "#d87b19", "#9043b3", "#b6322c"
im = Image.new("RGB", (W, H), bg)
d = ImageDraw.Draw(im)

def font(size, bold=False):
    return ImageFont.truetype("C:/Windows/Fonts/arialbd.ttf" if bold else "C:/Windows/Fonts/arial.ttf", size)

def t(x, y, value, size=22, color=ink, bold=False):
    d.text((x, y), value, font=font(size, bold), fill=color)

def rail(y, name, color, left, right):
    t(75, y-14, name, 21, color, True)
    d.line((350, y+9, 620, y+9), fill=color, width=8)
    for x in (375, 485, 595):
        d.ellipse((x-8, y+1, x+8, y+17), fill="white", outline=color, width=3)
    t(660, y-13, left, 20, ink, True)
    t(660, y+15, right, 19, muted)

t(55, 30, "BOARD A | SPACED 65-ROW LAYOUT", 43, ink, True)
t(56, 87, "JY V2 three-sensor wiring stays at rows 4-9; only Sonar B and radar move apart", 24, muted)
d.rounded_rectangle((50, 144, 1750, 407), radius=22, fill="white", outline="#cedbea", width=2)
t(75, 163, "POWER AND GROUND", 23, ink, True)
rail(208, "3V3 top +", green, "ESP 3V3 -> A4; B4 -> red rail P1", "Sonar B VCC -> P2 (same top rail section)")
rail(275, "GND blue -", blue, "ESP GND -> A7; B7 -> blue rail N1", "Sonar B GND -> N2; bridge N3 top -> N4 bottom; radar GND -> N5")
rail(342, "RADAR 5V", orange, "ESP 5V -> A54; LD2450 5V -> E54", "Dedicated row 54 only: never join 5V to row 4 or a GPIO")

board_left, board_right = 600, 1110
d.rounded_rectangle((board_left, 430, board_right, 1510), radius=20, fill="#eee8d5", outline="#aa9c77", width=3)
t(700, 449, "A   B   C   D   E", 27, ink, True)
t(54, 449, "ESP32-S3 N16R8 PIN -> HOLE", 23, green, True)
t(1150, 449, "SENSOR PIN -> HOLE", 23, purple, True)

entries = [
    (4, 515, "3V3 -> A4", ["B4 -> 3V3 rail P1; C4 Sonar A VCC", "D4 Light VCC; E4 AHT VCC"], green, "BCDE"),
    (5, 585, "GPIO1 SDA -> A5", ["D5 Light SDA; E5 AHT SDA"], green, "DE"),
    (6, 655, "GPIO2 SCL -> A6", ["D6 Light SCL; E6 AHT SCL"], green, "DE"),
    (7, 725, "GND -> A7", ["B7 -> GND rail N1; C7 Sonar A GND", "D7 Light GND; E7 AHT GND"], blue, "BCDE"),
    (8, 795, "GPIO4 -> A8", ["E8 Sonar A TRIG (timeout unresolved)"], orange, "E"),
    (9, 865, "GPIO5 -> A9", ["E9 Sonar A ECHO (timeout unresolved)"], orange, "E"),
    (28, 1010, "GPIO6 -> A28", ["E28 Sonar B TRIG"], purple, "E"),
    (29, 1080, "GPIO7 -> A29", ["E29 Sonar B ECHO"], purple, "E"),
    (50, 1260, "GPIO17 RX -> A50", ["E50 radar TX"], purple, "E"),
    (51, 1330, "GPIO18 TX -> A51", ["E51 radar RX (optional)"], purple, "E"),
    (54, 1400, "5V -> A54", ["E54 radar 5V ONLY"], orange, "E"),
]
xs = dict(zip("ABCDE", (690, 772, 854, 936, 1018)))
for row, y, esp, sensors, color, occupied in entries:
    t(55, y-14, esp, 22, color, True)
    t(620, y-15, f"row {row}", 19, muted)
    d.line((690, y+9, 1018, y+9), fill=color, width=2)
    for col, x in xs.items():
        radius = 10 if col == "A" or col in occupied else 5
        d.ellipse((x-radius, y+9-radius, x+radius, y+9+radius),
                  fill="white" if radius == 10 else ink,
                  outline=color if radius == 10 else ink, width=3 if radius == 10 else 1)
    for j, label in enumerate(sensors):
        t(1150, y-14+j*25, label, 20, color if color == purple else ink, color == purple)

t(635, 930, "Rows 10-27 left free", 20, muted, True)
t(635, 1165, "Rows 30-49 left free", 20, muted, True)
t(635, 1465, "Rows 55-65 left free", 20, muted, True)
t(57, 1532, "3V3 and GND rails are separate. Bridge only GND top-to-bottom across a center break.", 21, red, True)
t(57, 1565, "GPIO map and three_sensors.cfg stay unchanged. Unplug USB before moving any wires.", 21, muted)
t(57, 1598, "New Sonar B/radar positions are proposed; neither is validated on this spaced layout.", 21, muted)
im.save(out)
print(out)
