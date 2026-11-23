from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

OUT = Path(__file__).resolve().parent
BG = '#f4f7fb'
INK = '#152238'
MUTED = '#52657d'
BLUE = '#1d6ec9'
GREEN = '#13856b'
ORANGE = '#ca681d'
PURPLE = '#7952b3'
GRAY = '#9bacbf'

def font(size, bold=False):
    name = 'arialbd.ttf' if bold else 'arial.ttf'
    return ImageFont.truetype(str(Path('C:/Windows/Fonts') / name), size)

def label(d, xy, value, size=24, color=INK, bold=False):
    d.text(xy, value, fill=color, font=font(size, bold))

def roundrect(d, box, fill, outline=None, width=2, radius=20):
    d.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)

def base(title, subtitle, badge):
    im = Image.new('RGB', (1600, 1050), BG)
    d = ImageDraw.Draw(im)
    label(d, (54, 35), title, 42, INK, True)
    label(d, (56, 89), subtitle, 23, MUTED)
    roundrect(d, (1210, 36, 1546, 84), '#e8eff8')
    label(d, (1230, 47), badge, 20, BLUE, True)
    return im, d

def rails(d, mode):
    source = mode['source']
    roundrect(d, (55, 142, 1545, 328), 'white', '#d3deeb')
    label(d, (77, 160), 'BREADBOARD POWER RAILS', 25, INK, True)
    entries = [
        ('3V3 +', GREEN, source + ' 3V3  ->  ' + mode['3v3']),
        ('GND -', BLUE, source + ' GND  ->  ' + mode['gnd']),
    ]
    if '5v' in mode:
        entries.insert(1, ('5V +', ORANGE, source + ' 5V (USB)  ->  ' + mode['5v']))
    for idx, (name, color, desc) in enumerate(entries):
        y = 203 + idx * 39 if len(entries) == 3 else 215 + idx * 48
        label(d, (92, y - 2), name, 23, color, True)
        d.line((208, y + 14, 402, y + 14), fill=color, width=9)
        label(d, (427, y - 2), desc, 24, INK)

def rows(d, data, foot):
    roundrect(d, (55, 351, 1545, 921), 'white', '#d3deeb')
    label(d, (78, 372), 'BREADBOARD TERMINAL ROWS', 25, INK, True)
    label(d, (78, 407), 'On each numbered row, holes A-E are connected. Adjacent rows are separate.', 21, MUTED)
    label(d, (83, 454), 'ROW', 21, MUTED, True)
    label(d, (220, 454), 'N16R8 PIN', 21, MUTED, True)
    label(d, (655, 454), 'SAME CONNECTED ROW', 21, MUTED, True)
    label(d, (1110, 454), 'DEVICE PIN', 21, MUTED, True)
    top, gap = 493, min(57, int(385 / max(len(data), 1)))
    for idx, (row, esp, device, status) in enumerate(data):
        y = top + idx * gap
        if idx % 2 == 0:
            d.rectangle((75, y - 3, 1524, y + gap - 5), fill='#f4f8fd')
        color = PURPLE if status == 'PROPOSED' else GREEN
        label(d, (87, y + 4), str(row), 24, INK, True)
        label(d, (220, y + 4), esp, 23, color, True)
        # Five holes illustrate one electrically common A-E terminal group.
        d.line((655, y + 18, 995, y + 18), fill=color, width=3)
        for x in (665, 742, 819, 896, 973):
            d.ellipse((x-7, y+11, x+7, y+25), fill='white', outline=color, width=3)
        label(d, (1109, y + 4), device, 22, INK)
        if status == 'PROPOSED':
            label(d, (1397, y + 5), 'PLAN', 16, PURPLE, True)
    label(d, (80, 876), foot, 20, MUTED)

def footer(d, lines):
    for i, t in enumerate(lines):
        label(d, (58, 946 + i*30), t, 21, MUTED)

im, d = base('BOARD A  |  PHYSICAL INPUTS',
             'Bare N16R8; JY V2 rows 4-9 + Sonar B + radar',
             'V2 base + added pins')
rails(d, {
    'source': 'A header',
    '3v3': 'A4 -> B4 Sonar B, C4 Sonar A, D4 TSL, E4 AHT',
    '5v': 'R1 -> R2 LD2450 ONLY',
    'gnd': 'A7 -> B7 bridge -> N1 rail; N2 Sonar B, N3 radar',
})
rows(d, [
    (5, 'A5 GPIO1 SDA', 'D5 TSL + E5 AHT SDA', 'ESTABLISHED'),
    (6, 'A6 GPIO2 SCL', 'D6 TSL + E6 AHT SCL', 'ESTABLISHED'),
    (8, 'A8 GPIO4', 'E8 Sonar A TRIG', 'ESTABLISHED'),
    (9, 'A9 GPIO5', 'E9 Sonar A ECHO*', 'ESTABLISHED'),
    (10, 'A10 GPIO6', 'E10 Sonar B TRIG', 'PROPOSED'),
    (11, 'A11 GPIO7', 'E11 Sonar B ECHO*', 'PROPOSED'),
    (12, 'A12 GPIO17 RX', 'E12 Radar TX', 'PROPOSED'),
    (13, 'A13 GPIO18 TX', 'E13 Radar RX optional', 'PROPOSED'),
], 'Remove all old A LCD wires, including row 14; LCD belongs on C.')
footer(d, [
    '* Both sonars must have verified 3.3V ECHO. Connect B7 to GND rail N1.',
    'Keep the separate radar 5V rail away from row 4 / 3V3. Unplug USB to rewire.',
    'Earlier Super Mini readings do not verify the complete N16R8 fixture.',
])
im.save(OUT / 'board-a-inputs.png')

im, d = base('BOARD B  |  PROCESSING + STORAGE',
             'Bare N16R8 Board B; W25QXX external SPI flash',
             'N16R8 read-only SPI pass')
rails(d, {
    'source': 'B header',
    '3v3': 'W25QXX VCC',
    'gnd': 'W25QXX GND',
})
rows(d, [
    (10, 'GPIO10  CS', 'Flash CS', 'ESTABLISHED'),
    (11, 'GPIO11  SCK', 'Flash SLK / CLK', 'ESTABLISHED'),
    (12, 'GPIO12  MOSI', 'Flash DI / MOSI', 'ESTABLISHED'),
    (13, 'GPIO13  MISO', 'Flash DO / MISO', 'ESTABLISHED'),
], 'ESP B male header -> female jumper -> breadboard; SPIID EF4016 passed.')
footer(d, [
    'Confirm printed flash-module labels; do not infer pin order from wire color.',
    'SPICHECK passed; sector 3FF000 is NONBLANK. D6 write area remains undecided.',
    'B receives A inputs and sends C output over MQTT; no signal wire joins the boards.',
])
im.save(OUT / 'board-b-processing-flash.png')

im, d = base('BOARD C  |  LCD OUTPUT',
             'Bare N16R8 Board C; DIYUSER 76 x 284 ST7789P3 display',
             'LCDTEST passed on N16R8 C')
rails(d, {
    'source': 'C header',
    '3v3': 'LCD VCC',
    'gnd': 'LCD GND + LCD BL*',
})
rows(d, [
    (10, 'GPIO12  SPI SCK', 'LCD SCL / SCK', 'ESTABLISHED'),
    (11, 'GPIO11  SPI MOSI', 'LCD SDA / MOSI', 'ESTABLISHED'),
    (12, 'GPIO8', 'LCD RST', 'ESTABLISHED'),
    (13, 'GPIO9', 'LCD DC', 'ESTABLISHED'),
    (14, 'GPIO10', 'LCD CS', 'ESTABLISHED'),
], 'C male header -> female jumper -> breadboard; LCD SCL/SDA mean SPI.')
footer(d, [
    '* BL-to-GND applies only to the tested active-low module and existing fixture.',
    '18 Nov: LCDTEST result=0 and visible colours/READY on N16R8 C.',
    'C receives B output over MQTT; live B-to-C rendering is not yet implemented.',
])
im.save(OUT / 'board-c-lcd.png')

print(OUT)
