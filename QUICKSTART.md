# Quick Start Guide

## 1. Hardware Setup

Connect components according to `PINOUT.md`:
- LoRa module to SPI pins (D9-D13, D4)
- RTC to I2C pins (A4, A5)
- SSRs to D2 and D3

## 2. Software Setup

1. Install Arduino IDE
2. Install libraries:
   - LoRa (Sandeep Mistry)
   - RTClib (Adafruit)
3. Open `WaterPump_Slave.ino`
4. Select board: **Arduino Nano**
5. Upload code

## 3. Initial Configuration

Open Serial Monitor (115200 baud) and run:

```
settime 2024,1,15,14,30,0
```

This sets the RTC to January 15, 2024, 14:30:00.

## 4. Create Your First Schedule

Example: Run Pump 0 every day at 8:30 AM for 60 minutes

```
set 0,1,0,8,30,60,127
```

Breakdown:
- `0` = Schedule slot 0
- `1` = Enabled
- `0` = Pump 0 (SSR on D2)
- `8` = Hour (8 AM)
- `30` = Minute
- `60` = Duration (60 minutes)
- `127` = All days (1+2+4+8+16+32+64)

## 5. Verify Setup

```
status    # Check system status
time      # Check current time
list      # List all schedules
```

## 6. Test Pump Manually

```
run 0,5   # Run pump 0 for 5 minutes
stop 0    # Stop pump 0
```

## Common Schedule Examples

### Weekdays Only (Mon-Fri) at 6 AM for 30 minutes
```
set 0,1,0,6,0,30,62
```
(62 = 2+4+8+16+32 = Mon-Fri)

### Weekends Only (Sat-Sun) at 9 AM for 45 minutes
```
set 1,1,1,9,0,45,65
```
(65 = 1+64 = Sun+Sat)

### Twice Daily (8 AM and 6 PM) for 1 hour
```
set 0,1,0,8,0,60,127   # Morning
set 1,1,0,18,0,60,127  # Evening
```

## Master Communication

Once configured, the slave automatically:
- Receives commands from master via LoRa
- Executes schedules based on RTC time
- Sends status updates to master

No further action needed!

## Troubleshooting

**No response from Serial:**
- Check baud rate: 115200
- Verify USB connection
- Check Arduino is powered

**RTC shows wrong time:**
- Run `settime` command with correct time
- Check RTC battery (if applicable)

**Pump doesn't start:**
- Check schedule: `list`
- Verify time: `time`
- Check pump manually: `run 0,1`
- Verify SSR connections

**LoRa not working:**
- Check frequency setting (915 MHz default)
- Verify antenna connection
- Check master is in range
- Use `status` to check LoRa connection

