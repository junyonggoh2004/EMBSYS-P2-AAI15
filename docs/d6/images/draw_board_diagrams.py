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
    roundrect(d, (55, 142, 1545, 328), 'white', '#d3deeb')
    label(d, (77, 160), 'BREADBOARD POWER RAILS', 25, INK, True)
    entries = [
        ('3V3 +', GREEN, 'ESP 3V3  ->  ' + mode['3v3']),
        ('GND -', BLUE, 'ESP GND  ->  ' + mode['gnd']),
    ]
    if '5v' in mode:
        entries.insert(1, ('5V +', ORANGE, 'ESP 5V (USB power)  ->  ' + mode['5v']))
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
    label(d, (220, 454), 'ESP32-S3 PIN', 21, MUTED, True)
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
             'One ESP32-S3 Super Mini; AHT + light + Sonar A/B + LD2450 radar',
             'Established + proposed pins')
rails(d, {
    '3v3': 'AHT VCC, TSL2561 VCC, Sonar A/B VCC*',
    '5v': 'LD2450 5V ONLY',
    'gnd': 'AHT, TSL2561, Sonar A/B, LD2450 GND',
})
rows(d, [
    (10, 'GPIO1  SDA', 'AHT SDA + TSL SDA', 'ESTABLISHED'),
    (11, 'GPIO2  SCL', 'AHT SCL + TSL SCL', 'ESTABLISHED'),
    (15, 'GPIO4', 'Sonar A TRIG', 'ESTABLISHED'),
    (16, 'GPIO5', 'Sonar A ECHO*', 'ESTABLISHED'),
    (18, 'GPIO6', 'Sonar B TRIG', 'PROPOSED'),
    (19, 'GPIO7', 'Sonar B ECHO*', 'PROPOSED'),
    (22, 'GPIO17  UART RX', 'Radar TX  ->  ESP RX', 'PROPOSED'),
    (23, 'GPIO18  UART TX', 'Radar RX  <-  ESP TX', 'PROPOSED'),
], 'Purple PLAN rows require team pin approval; GPIO17/18 are underside pads on HW-747.')
footer(d, [
    '* Use only verified 3.3 V-compatible sonar modules; protect any 5 V ECHO.',
    'Keep the 5 V rail isolated from 3V3 and GPIO. Unplug USB before wiring; confirm rail continuity.',
    'Radar power: USB-backed ESP 5V as discussed; check current budget and common ground.',
])
im.save(OUT / 'board-a-inputs.png')

im, d = base('BOARD B  |  PROCESSING + STORAGE',
             'One ESP32-S3 Super Mini; W25QXX external SPI flash module',
             'Tested D2 flash pin map')
rails(d, {
    '3v3': 'W25QXX VCC',
    'gnd': 'W25QXX GND',
})
rows(d, [
    (10, 'GPIO10  CS', 'Flash CS', 'ESTABLISHED'),
    (11, 'GPIO11  SCK', 'Flash SLK / CLK', 'ESTABLISHED'),
    (12, 'GPIO12  MOSI', 'Flash DI / MOSI', 'ESTABLISHED'),
    (13, 'GPIO13  MISO', 'Flash DO / MISO', 'ESTABLISHED'),
], 'This is the six-wire D2 flash fixture, including 3V3 and GND above.')
footer(d, [
    'Confirm printed flash-module labels; do not infer pin order from wire color.',
    'Begin with read-only JEDEC ID. D6 record format and writable address remain undecided.',
    'B receives A inputs and sends C output over MQTT; no signal wire joins the boards.',
])
im.save(OUT / 'board-b-processing-flash.png')

im, d = base('BOARD C  |  LCD OUTPUT',
             'One ESP32-S3 Super Mini; DIYUSER 76 x 284 ST7789P3 display',
             'Verified isolated LCD pin map')
rails(d, {
    '3v3': 'LCD VCC',
    'gnd': 'LCD GND + LCD BL*',
})
rows(d, [
    (10, 'GPIO12  SPI SCK', 'LCD SCL / SCK', 'ESTABLISHED'),
    (11, 'GPIO11  SPI MOSI', 'LCD SDA / MOSI', 'ESTABLISHED'),
    (12, 'GPIO8', 'LCD RST', 'ESTABLISHED'),
    (13, 'GPIO9', 'LCD DC', 'ESTABLISHED'),
    (14, 'GPIO10', 'LCD CS', 'ESTABLISHED'),
], 'LCD SCL/SDA are SPI signals, not the I2C bus on Board A.')
footer(d, [
    '* BL-to-GND applies only to the tested active-low module and existing fixture.',
    'Run LCDTEST 76 284 82 18 00 0 on C before attempting live display.',
    'C receives B output over MQTT; live B-to-C rendering is not yet implemented.',
])
im.save(OUT / 'board-c-lcd.png')

print(OUT)
