# Water Pump Scheduler - Slave Node

Industrial-grade Arduino Nano slave code for LoRa-based water pump scheduling system.

## Hardware Configuration

### Arduino Nano Pinout

| Component | Pin | Description |
|-----------|-----|-------------|
| **LoRa Module (SX1278/RA02)** | | |
| RST | D9 | Reset pin |
| DIO0 | D4 | Interrupt pin (changed from D2 to avoid conflict) |
| NSS | D10 | Chip select |
| MOSI | D11 | SPI data out |
| MISO | D12 | SPI data in |
| SCK | D13 | SPI clock |
| **RTC (DS3231)** | | |
| SDA | A4 | I2C data |
| SCL | A5 | I2C clock |
| **SSR (Solid State Relays)** | | |
| SSR 1 | D2 | Pump 1 control |
| SSR 2 | D3 | Pump 2 control |

**Note:** DIO0 was moved to D4 to avoid pin conflict with SSR on D2.

## Required Libraries

Install these libraries via Arduino Library Manager:

1. **LoRa** by Sandeep Mistry
   - Library Manager: Search "LoRa"
   - GitHub: https://github.com/sandeepmistry/arduino-LoRa

2. **RTClib** by Adafruit
   - Library Manager: Search "RTClib"
   - GitHub: https://github.com/adafruit/RTClib

3. **Wire** - Built-in Arduino library (I2C)
4. **SPI** - Built-in Arduino library
5. **EEPROM** - Built-in Arduino library

## Features

### Core Functionality
- ✅ LoRa communication with master node
- ✅ RTC-based precise scheduling (1-minute resolution)
- ✅ EEPROM persistence (survives power loss)
- ✅ Dual pump control (2 SSR outputs)
- ✅ Serial CLI for direct hardware access
- ✅ Industrial-grade error handling

### Communication Protocol

#### LoRa Commands (from Master)
- `SET_SCHEDULE:index,enabled,pump_id,hour,minute,duration,days`
- `DELETE_SCHEDULE:index`
- `CLEAR_ALL:`
- `RUN_TIMER:pump_id,duration_min`
- `STOP:` or `STOP:pump_id`
- `GET_STATUS:`
- `SYNC_TIME:YYYY,MM,DD,HH,MM,SS`

#### LoRa Responses
- `ACK:OK` - Command successful
- `ACK:ERROR` - Command failed
- `STATUS:RTC=OK,SCHED=5,P1=ON,P2=OFF` - Status response

### Serial CLI Commands

| Command | Description | Example |
|---------|-------------|---------|
| `help` | Show command help | `help` |
| `status` | Show system status | `status` |
| `time` | Show current RTC time | `time` |
| `set` | Set schedule | `set 0,1,0,8,30,60,127` |
| `list` | List all schedules | `list` |
| `clear` | Clear schedule | `clear 0` |
| `clear all` | Clear all schedules | `clear all` |
| `run` | Run pump timer | `run 0,30` |
| `stop` | Stop pump(s) | `stop` or `stop 0` |
| `settime` | Set RTC time | `settime 2024,1,15,14,30,0` |

### Schedule Format

```
set <index>,<enabled>,<pump_id>,<hour>,<minute>,<duration>,<days>
```

- **index**: 0-9 (schedule slot)
- **enabled**: 0=disabled, 1=enabled
- **pump_id**: 0 or 1 (SSR 1 or SSR 2)
- **hour**: 0-23
- **minute**: 0-59
- **duration**: 1-255 minutes
- **days**: Bitmask (1=Sun, 2=Mon, 4=Tue, 8=Wed, 16=Thu, 32=Fri, 64=Sat)
  - Example: 127 = all days (1+2+4+8+16+32+64)
  - Example: 31 = Mon-Fri (2+4+8+16+32)

## EEPROM Structure

- **Address 0x00-0x01**: Magic number (0xABCD)
- **Address 0x02**: Data version (1)
- **Address 0x10+**: Schedule entries (16 bytes each, max 10 schedules)

## LoRa Configuration

Default settings (adjustable in code):
- **Frequency**: 915 MHz (change `LoRa.begin(915E6)` for your region)
- **Sync Word**: 0xF3
- **Spreading Factor**: 12
- **Bandwidth**: 125 kHz
- **Coding Rate**: 5
- **TX Power**: 20 dBm

**Regional Frequencies:**
- 433 MHz: Europe, Asia
- 868 MHz: Europe
- 915 MHz: North America, Australia
- 923 MHz: Asia (some regions)

## Installation

1. Install Arduino IDE (1.8.x or 2.x)
2. Install required libraries (see above)
3. Connect hardware according to pinout
4. Select board: **Arduino Nano**
5. Select processor: **ATmega328P (Old Bootloader)** or **ATmega328P**
6. Upload code

## Usage

### Initial Setup

1. Upload code to Arduino Nano
2. Open Serial Monitor (115200 baud)
3. Set RTC time: `settime 2024,1,15,14,30,0`
4. Create schedule: `set 0,1,0,8,30,60,127`

### Testing

1. Check status: `status`
2. Check time: `time`
3. Test pump: `run 0,5` (runs pump 0 for 5 minutes)
4. Stop pump: `stop 0`
5. List schedules: `list`

### Master Communication

The slave automatically receives commands from the master via LoRa. No manual intervention needed once configured.

## Troubleshooting

### RTC Not Found
- Check I2C connections (SDA=A4, SCL=A5)
- Verify RTC module is DS3231
- Check power supply

### LoRa Not Initializing
- Check SPI connections
- Verify NSS pin (D10)
- Check frequency setting matches your region
- Ensure LoRa module is powered (3.3V)

### Schedules Not Triggering
- Verify RTC time is correct
- Check schedule is enabled
- Verify day-of-week bitmask
- Check Serial Monitor for trigger messages

### EEPROM Issues
- EEPROM is automatically initialized on first run
- Data persists across power cycles
- Use `clear all` to reset all schedules

## Code Architecture

### Main Components
- **Schedule Processing**: Checks RTC every second, triggers schedules
- **Timer Management**: Tracks active pump timers, auto-stops on expiry
- **LoRa Handler**: Receives commands, sends acknowledgments
- **Serial CLI**: Direct hardware access and debugging
- **EEPROM Manager**: Loads/saves schedules persistently

### Timing
- RTC check: Every 1 second
- LoRa check: Every 100ms
- Status report: Every 30 seconds
- Schedule save: On modification (debounced)

## Safety Features

- Input validation on all commands
- Automatic pump stop on timer expiry
- EEPROM corruption detection (magic number)
- Error reporting via Serial and LoRa
- Watchdog-ready architecture

## License

Industrial use - modify as needed for your application.

## Support

For issues or questions, check:
- Serial Monitor output for error messages
- LoRa signal strength
- RTC battery status
- Power supply stability

