# Pin Configuration Reference

## Arduino Nano Pinout

### Digital Pins

| Pin | Function | Component | Notes |
|-----|----------|-----------|-------|
| D2 | Output | SSR 1 (Pump 1) | HIGH = Pump ON |
| D3 | Output | SSR 2 (Pump 2) | HIGH = Pump ON |
| D4 | Input (Interrupt) | LoRa DIO0 | Interrupt pin for LoRa |
| D9 | Output | LoRa RST | Reset control |
| D10 | Output | LoRa NSS | SPI Chip Select |
| D11 | Output | LoRa MOSI | SPI Data Out |
| D12 | Input | LoRa MISO | SPI Data In |
| D13 | Output | LoRa SCK | SPI Clock |

### Analog Pins (I2C)

| Pin | Function | Component | Notes |
|-----|----------|-----------|-------|
| A4 | I2C SDA | RTC SDA | I2C Data Line |
| A5 | I2C SCL | RTC SCL | I2C Clock Line |

## Pin Conflict Resolution

**Original Specification:**
- LoRa DIO0: D2
- SSR 1: D2

**Resolution:**
- LoRa DIO0 moved to D4 (interrupt-capable pin)
- SSR 1 remains on D2

**Why D4?**
- D4 is INT4 on ATmega328P (interrupt-capable)
- D2 is INT0, but needed for SSR output
- D4 provides same interrupt functionality for LoRa

## Hardware Connections

### LoRa Module (SX1278/RA02)
```
LoRa Module    →    Arduino Nano
─────────────────────────────────
VCC            →    3.3V (NOT 5V!)
GND            →    GND
RST            →    D9
DIO0           →    D4 (changed from D2)
NSS            →    D10
MOSI           →    D11
MISO           →    D12
SCK            →    D13
```

### RTC Module (DS3231)
```
RTC Module     →    Arduino Nano
─────────────────────────────────
VCC            →    5V
GND            →    GND
SDA            →    A4
SCL            →    A5
```

### SSR Modules
```
SSR 1          →    Arduino Nano
─────────────────────────────────
Control        →    D2
VCC            →    5V (or external)
GND            →    GND
Load           →    Pump 1

SSR 2          →    Arduino Nano
─────────────────────────────────
Control        →    D3
VCC            →    5V (or external)
GND            →    GND
Load           →    Pump 2
```

## Power Requirements

- **Arduino Nano**: 7-12V via VIN or 5V via USB
- **LoRa Module**: 3.3V (use voltage regulator if needed)
- **RTC Module**: 5V (most modules have built-in regulator)
- **SSR Modules**: Check SSR specifications (typically 3-32V DC control)

## Important Notes

1. **LoRa Voltage**: Most SX1278 modules require 3.3V, NOT 5V. Use a voltage regulator or level shifter if your module doesn't have one built-in.

2. **SSR Control**: SSRs typically require 3-5V DC on the control pin. Arduino Nano outputs 5V on digital pins, which is usually compatible.

3. **I2C Pull-ups**: RTC modules usually have built-in pull-up resistors. If not, add 4.7kΩ resistors on SDA and SCL.

4. **SPI Sharing**: If you need to use other SPI devices, ensure proper NSS (chip select) management.

## Changing Pin Configuration

To change pins, modify the `#define` statements in `WaterPump_Slave.ino`:

```cpp
#define SSR_PIN_1           2   // Change to desired pin
#define SSR_PIN_2           3   // Change to desired pin
#define LORA_DIO0           4   // Must be interrupt-capable (D2, D3, D4, D5, D6, D7)
```

**Interrupt-capable pins on Arduino Nano:**
- D2 (INT0)
- D3 (INT1)
- D4-D7 (PCINT, requires additional setup)

For LoRa DIO0, D2, D3, or D4 are recommended.

