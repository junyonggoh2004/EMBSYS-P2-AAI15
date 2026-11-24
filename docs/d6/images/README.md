# D6 board diagrams

| Image | What it shows |
|---|---|
| [Board A wiring](board-a-sonar-only.svg) | Board A (`s3-001`): AHT and TSL2561 on I2C (SDA GPIO1, SCL GPIO2), Sonar A on GPIO10/11 and Sonar B on GPIO6/7 with direct power leads. Connection table: [BOARD_A_SONAR_ONLY_WIRING.md](../BOARD_A_SONAR_ONLY_WIRING.md) |
| [Board B processing and flash](board-b-processing-flash.png) | Board B (`s3-002`) with the W25Q32 flash on SPI: CS GPIO10, CLK GPIO11, DI GPIO12, DO GPIO13 |
| [Board C LCD](board-c-lcd.png) | Board C (`s3-003`) with the ST7789 LCD on SPI: SCK GPIO12, MOSI GPIO11, CS GPIO10, DC GPIO9, RST GPIO8 |
| [Room monitor reference](room-monitor-mock-reference.png) | The landscape layout Board C's screen follows; see the [display reference](../ROOM_MONITOR_REFERENCE.md) |

[draw_board_diagrams.py](draw_board_diagrams.py) redraws the Board B and C images with Pillow.
