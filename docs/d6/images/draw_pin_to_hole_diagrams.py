from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent
OUT.mkdir(parents=True, exist_ok=True)
BG = '#f5f7fb'
INK = '#18304d'
MUTED = '#415a77'
BOARD = '#eee8d5'
GREEN = '#237c3e'
BLUE = '#177db9'
ORANGE = '#d87b19'
PURPLE = '#9043b3'
RED = '#b6322c'

def f(n, bold=False):
    return ImageFont.truetype('C:/Windows/Fonts/arialbd.ttf' if bold else 'C:/Windows/Fonts/arial.ttf', n)

def txt(d, xy, value, n=23, color=INK, bold=False):
    d.text(xy, value, font=f(n, bold), fill=color)

def line(d, xy, color, width=4):
    d.line(xy, fill=color, width=width)

def dot(d, x, y, color, radius=11):
    d.ellipse((x-radius, y-radius, x+radius, y+radius), fill='white', outline=color, width=4)

def frame(title, subtitle, tag, h):
    im = Image.new('RGB', (1700, h), BG)
    d = ImageDraw.Draw(im)
    txt(d, (50, 30), title, 42, INK, True)
    txt(d, (51, 86), subtitle, 22, MUTED)
    d.rounded_rectangle((1280, 37, 1650, 90), radius=22, fill='#e7eff8')
    txt(d, (1300, 51), tag, 19, BLUE, True)
    return im, d

def power(d, blocks, y0=150):
    h = 85 * len(blocks) + 18
    d.rounded_rectangle((50, y0, 1650, y0+h), radius=20, fill='white', outline='#d1ddeb', width=2)
    txt(d, (71, y0+17), 'POWER RAILS - HOLE NAMES ARE EXAMPLES', 21, INK, True)
    for i, (name, color, left, right) in enumerate(blocks):
        y = y0 + 58 + i*85
        line(d, (80, y+20, 400, y+20), color, 8)
        dot(d, 120, y+20, color, 8)
        dot(d, 245, y+20, color, 8)
        dot(d, 370, y+20, color, 8)
        txt(d, (83, y-11), name, 20, color, True)
        txt(d, (425, y-9), left, 21, INK, True)
        for j, value in enumerate(right):
            txt(d, (870, y-13+j*25), value, 19, INK)
    return y0+h

def breadboard(d, rows, top, bottom, note, esp_label):
    x0, x1 = 550, 1130
    d.rounded_rectangle((x0, top, x1, bottom), radius=20, fill=BOARD, outline='#aa9c77', width=3)
    txt(d, (682, top+22), 'A   B   C   D   E', 27, INK, True)
    txt(d, (50, top+18), esp_label, 22, GREEN, True)
    txt(d, (1160, top+18), 'SENSOR / MODULE PINS', 22, PURPLE, True)
    label_top = top+80
    gap = min(82, int((bottom-top-145)/len(rows)))
    for i, row in enumerate(rows):
        number, left, pins, color, proposal = row
        y = label_top+i*gap
        txt(d, (50, y-10), left, 22, color if proposal else GREEN, True)
        txt(d, (583, y-12), f'row {number}', 19, MUTED)
        for col, x in zip('ABCDE', (720, 802, 884, 966, 1048)):
            connected = col == 'A' or col in pins
            dot(d, x, y+8, color if connected else '#263b4f', 10 if connected else 5)
        line(d, (720, y+8, 1048, y+8), color, 2)
        for j, (hole, value) in enumerate(pins.items()):
            txt(d, (1160, y-11+j*26), f'{hole}{number} <- {value}', 20, color if proposal else INK, proposal)
        if proposal:
            txt(d, (52, y+18), 'PROPOSED PIN', 15, PURPLE, True)
    txt(d, (581, bottom-36), 'A-E holes connect across each numbered row.', 18, MUTED)
    txt(d, (50, bottom+22), note, 21, MUTED)

# A: JY Version 2 rows retained for three sensors, with new Sonar B/radar rows.
im, d = frame('BOARD A | JY V2 BASE + TWO INPUTS',
              'Bare ESP32-S3 N16R8; ESP and modules sit outside the breadboard',
              'AHT / LIGHT / A + B / RADAR', 1460)
edge = power(d, [
    ('3V3 VIA ROW 4', GREEN, 'ESP 3V3 -> A4', [
        'B4 Sonar B; C4 Sonar A; D4 TSL2561; E4 AHT']),
    ('BLUE GND RAIL', BLUE, 'ESP GND -> A7; B7 -> N1', [
        'C7 Sonar A; D7 TSL2561; E7 AHT',
        'N2 Sonar B; N3 radar; B7-N1 is a jumper']),
    ('SEPARATE 5V RAIL', ORANGE, 'ESP 5V -> R1 (USB)', [
        'LD2450 radar 5V -> R2 ONLY',
        'Never join 5V to row 4, 3V3 or GPIO']),
])
breadboard(d, [
    (5, 'ESP GPIO1 (SDA) -> A5', {'D':'TSL SDA', 'E':'AHT SDA'}, GREEN, False),
    (6, 'ESP GPIO2 (SCL) -> A6', {'D':'TSL SCL', 'E':'AHT SCL'}, GREEN, False),
    (8, 'ESP GPIO4 -> A8', {'E':'Sonar A TRIG'}, GREEN, False),
    (9, 'ESP GPIO5 -> A9', {'E':'Sonar A ECHO'}, GREEN, False),
    (10, 'ESP GPIO6 -> A10', {'E':'Sonar B TRIG'}, PURPLE, True),
    (11, 'ESP GPIO7 -> A11', {'E':'Sonar B ECHO'}, PURPLE, True),
    (12, 'ESP GPIO17 (RX) -> A12', {'E':'Radar TX'}, PURPLE, True),
    (13, 'ESP GPIO18 (TX) -> A13', {'E':'Radar RX (optional)'}, PURPLE, True),
], edge+28, 1265, 'Rows 4-9 preserve JY V2. Remove all old A LCD wires; add inputs on 10-13.',
   'A N16R8 MALE HEADER PINS')
txt(d, (51, 1320), 'Power off to rewire. Keep radar 5V separate. Both sonars need 3.3V ECHO.', 21, RED, True)
txt(d, (51, 1355), 'Purple = added proposed GPIOs; UART TX/RX header labels are NOT GPIO17/18.', 20, MUTED)
txt(d, (51, 1390), 'Historical Super Mini success does not prove a complete N16R8 sensor pass.', 20, MUTED)
im.save(OUT / 'board-a-detailed.png')

# B: tested D2 external flash wiring, separate from Board C LCD SPI.
im, d = frame('BOARD B | PROCESSING + SPI FLASH',
              'Bare N16R8 Board B + 3.3V W25QXX breakout',
              'No LCD on this board', 960)
edge = power(d, [
    ('BLUE GND RAIL', BLUE, 'ESP GND -> N1', ['Flash GND -> N2']),
    ('3V3 VIA ROW 4', GREEN, 'ESP 3V3 -> A4', ['Flash VCC -> E4 (same A-E row)'])
])
breadboard(d, [
    (4, 'ESP 3V3 -> A4', {'E':'Flash VCC'}, GREEN, False),
    (10, 'ESP GPIO10 (CS) -> A10', {'E':'Flash CS / SS'}, GREEN, False),
    (11, 'ESP GPIO11 (SCK) -> A11', {'E':'Flash SLK / CLK'}, GREEN, False),
    (12, 'ESP GPIO12 (MOSI) -> A12', {'E':'Flash DI / SI'}, GREEN, False),
    (13, 'ESP GPIO13 (MISO) -> A13', {'E':'Flash DO / SO'}, GREEN, False),
], edge+28, 820, 'ESP B male header -> female jumper -> A-row hole.', 'B N16R8 MALE HEADERS')
txt(d, (51, 885), 'N16R8 B: SPIID EF4016 + SPICHECK PASS. Sector 3FF000 NONBLANK; no writes.', 21, RED, True)
txt(d, (51, 920), 'A second SPI device for the later swap demo is still to be selected.', 20, MUTED)
im.save(OUT / 'board-b-detailed.png')

# C: known working isolated LCD wiring.
im, d = frame('BOARD C | ST7789 LCD OUTPUT',
              'Bare N16R8 Board C + DIYUSER 76 x 284 LCD; isolated test passed',
              'LCDTEST passed on N16R8 C', 1100)
edge = power(d, [
    ('BLUE GND RAIL', BLUE, 'ESP GND -> N1', ['LCD GND -> N2; LCD BL -> N3']),
    ('3V3 VIA ROW 4', GREEN, 'ESP 3V3 -> A4', ['LCD VCC -> E4 (same A-E row)'])
])
breadboard(d, [
    (4, 'ESP 3V3 -> A4', {'E':'LCD VCC'}, GREEN, False),
    (10, 'ESP GPIO12 (SCK) -> A10', {'E':'LCD SCL / SCK'}, GREEN, False),
    (11, 'ESP GPIO11 (MOSI) -> A11', {'E':'LCD SDA / MOSI'}, GREEN, False),
    (12, 'ESP GPIO8 -> A12', {'E':'LCD RST'}, GREEN, False),
    (13, 'ESP GPIO9 -> A13', {'E':'LCD DC'}, GREEN, False),
    (14, 'ESP GPIO10 -> A14', {'E':'LCD CS'}, GREEN, False),
], edge+28, 955, 'C male header -> female jumper -> A-row hole; LCD uses SPI.', 'C N16R8 MALE HEADERS')
txt(d, (51, 1017), 'BL -> GND is specific to this tested active-low LCD module.', 21, RED, True)
txt(d, (51, 1050), '18 Nov: LCDTEST result=0 and visible colours/READY; live values still need firmware.', 20, MUTED)
im.save(OUT / 'board-c-detailed.png')

print(OUT)
