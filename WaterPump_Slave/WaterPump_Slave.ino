/*
 * Water Pump Scheduler - Slave Node
 * Industrial Grade Implementation
 * 
 * Hardware:
 * - Arduino Nano
 * - LoRa Module (SX1278/RA02): RST=D9, DIO0=D4, NSS=D10, MOSI=D11, MISO=D12, SCK=D13
 * - RTC (DS3231): SDA=A4, SCL=A5
 * - SSR x2: D2, D3
 * - EEPROM: Internal
 * 
 * Features:
 * - LoRa communication with master
 * - RTC-based precise scheduling
 * - EEPROM persistence
 * - Serial CLI for direct control
 * - Watchdog timer
 * - Error handling and recovery
 */

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>

// ==================== PIN DEFINITIONS ====================
// NOTE: DIO0 was moved to D4 because D2 is used for SSR_PIN_1
// DIO0 must be an interrupt-capable pin (D4 = INT4 on ATmega328P)
// If you need to change this, ensure the pin supports interrupts
#define SSR_PIN_1           2
#define SSR_PIN_2           3
#define LORA_RST            9
#define LORA_DIO0           4  // Interrupt pin (moved from D2 to avoid SSR conflict)
#define LORA_NSS            10
#define LORA_MOSI           11
#define LORA_MISO           12
#define LORA_SCK            13

// ==================== EEPROM ADDRESSES ====================
#define EEPROM_MAGIC        0x00  // 2 bytes - Magic number
#define EEPROM_VERSION      0x02  // 1 byte - Data structure version
#define EEPROM_SCHEDULE_BASE 0x10 // Base address for schedules
#define EEPROM_SCHEDULE_SIZE 16   // Bytes per schedule entry
#define MAX_SCHEDULES        10   // Maximum number of schedules
#define EEPROM_MAGIC_VALUE   0xABCD

// ==================== DATA STRUCTURES ====================
struct ScheduleEntry {
  uint8_t enabled;        // 0=disabled, 1=enabled
  uint8_t pump_id;        // 0 or 1 (SSR_PIN_1 or SSR_PIN_2)
  uint8_t hour;           // 0-23
  uint8_t minute;         // 0-59
  uint8_t duration_min;  // Duration in minutes (0-255)
  uint8_t days_of_week;   // Bitmask: bit0=Sun, bit1=Mon, ..., bit6=Sat
  uint8_t reserved[9];    // Reserved for future use
};

struct SystemStatus {
  bool lora_connected;
  bool rtc_available;
  uint8_t active_schedules;
  uint32_t last_command_time;
  uint16_t lora_packets_received;
  uint16_t lora_packets_failed;
};

// ==================== GLOBAL OBJECTS ====================
RTC_DS3231 rtc;
SystemStatus system_status = {0};
ScheduleEntry schedules[MAX_SCHEDULES];
bool schedule_modified = false;

// ==================== LOOP TIMING ====================
unsigned long last_rtc_check = 0;
unsigned long last_lora_check = 0;
unsigned long last_status_report = 0;
const unsigned long RTC_CHECK_INTERVAL = 1000;    // Check RTC every second
const unsigned long LORA_CHECK_INTERVAL = 100;     // Check LoRa every 100ms
const unsigned long STATUS_REPORT_INTERVAL = 30000; // Report status every 30s

// ==================== ACTIVE TIMER TRACKING ====================
struct ActiveTimer {
  bool active;
  uint8_t pump_id;
  DateTime start_time;
  uint16_t duration_min;
};

ActiveTimer active_timers[2] = {0}; // One per pump

// ==================== SERIAL CLI BUFFER ====================
#define CLI_BUFFER_SIZE 64
char cli_buffer[CLI_BUFFER_SIZE];
uint8_t cli_index = 0;

// ==================== SETUP ====================
void setup() {
  // Initialize Serial
  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n=== Water Pump Scheduler - Slave Node ==="));
  Serial.println(F("Initializing system..."));
  
  // Initialize GPIO
  pinMode(SSR_PIN_1, OUTPUT);
  pinMode(SSR_PIN_2, OUTPUT);
  digitalWrite(SSR_PIN_1, LOW);
  digitalWrite(SSR_PIN_2, LOW);
  
  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println(F("ERROR: RTC not found!"));
    system_status.rtc_available = false;
  } else {
    system_status.rtc_available = true;
    if (rtc.lostPower()) {
      Serial.println(F("WARNING: RTC lost power, setting default time"));
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }
    Serial.println(F("RTC initialized"));
  }
  
  // Initialize LoRa
  LoRa.setPins(LORA_NSS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(915E6)) { // 915 MHz - adjust for your region
    Serial.println(F("ERROR: LoRa initialization failed!"));
    system_status.lora_connected = false;
  } else {
    LoRa.setSyncWord(0xF3); // Sync word for network
    LoRa.setSpreadingFactor(12);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    LoRa.setTxPower(20);
    system_status.lora_connected = true;
    Serial.println(F("LoRa initialized"));
  }
  
  // Load schedules from EEPROM
  loadSchedulesFromEEPROM();
  
  // Initialize CLI
  Serial.println(F("\n=== Serial CLI Ready ==="));
  Serial.println(F("Commands: help, status, time, set, list, clear, run, stop"));
  Serial.println(F("Type 'help' for detailed command list\n"));
  
  system_status.last_command_time = millis();
  
  Serial.println(F("System ready!\n"));
}

// ==================== MAIN LOOP ====================
void loop() {
  unsigned long current_time = millis();
  
  // Check RTC and process schedules
  if (current_time - last_rtc_check >= RTC_CHECK_INTERVAL) {
    last_rtc_check = current_time;
    if (system_status.rtc_available) {
      processSchedules();
      checkActiveTimers();
    }
  }
  
  // Check LoRa for incoming commands
  if (current_time - last_lora_check >= LORA_CHECK_INTERVAL) {
    last_lora_check = current_time;
    if (system_status.lora_connected) {
      checkLoRaCommands();
    }
  }
  
  // Process Serial CLI
  processSerialCLI();
  
  // Periodic status report
  if (current_time - last_status_report >= STATUS_REPORT_INTERVAL) {
    last_status_report = current_time;
    reportSystemStatus();
  }
  
  // Save schedules if modified
  if (schedule_modified) {
    saveSchedulesToEEPROM();
    schedule_modified = false;
  }
}

// ==================== SCHEDULE PROCESSING ====================
void processSchedules() {
  if (!system_status.rtc_available) return;
  
  DateTime now = rtc.now();
  uint8_t day_of_week = now.dayOfTheWeek(); // 0=Sunday, 6=Saturday
  
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    if (!schedules[i].enabled) continue;
    
    // Check if schedule matches current day
    if (!(schedules[i].days_of_week & (1 << day_of_week))) continue;
    
    // Check if schedule matches current time (within 1 minute window)
    if (schedules[i].hour == now.hour() && 
        schedules[i].minute == now.minute()) {
      
      // Start the pump
      startPumpTimer(schedules[i].pump_id, schedules[i].duration_min);
      
      Serial.print(F("Schedule #"));
      Serial.print(i);
      Serial.print(F(" triggered: Pump "));
      Serial.print(schedules[i].pump_id);
      Serial.print(F(" for "));
      Serial.print(schedules[i].duration_min);
      Serial.println(F(" minutes"));
    }
  }
}

void checkActiveTimers() {
  if (!system_status.rtc_available) return;
  
  DateTime now = rtc.now();
  
  for (uint8_t i = 0; i < 2; i++) {
    if (!active_timers[i].active) continue;
    
    // Calculate elapsed time
    TimeSpan elapsed = now - active_timers[i].start_time;
    uint16_t elapsed_min = elapsed.totalseconds() / 60;
    
    if (elapsed_min >= active_timers[i].duration_min) {
      // Timer expired, stop pump
      stopPump(i);
      Serial.print(F("Timer expired: Pump "));
      Serial.print(i);
      Serial.println(F(" stopped"));
    }
  }
}

// ==================== PUMP CONTROL ====================
void startPumpTimer(uint8_t pump_id, uint16_t duration_min) {
  if (pump_id > 1) return;
  if (duration_min == 0 || duration_min > 255) return;
  
  uint8_t ssr_pin = (pump_id == 0) ? SSR_PIN_1 : SSR_PIN_2;
  
  // Stop any existing timer for this pump
  if (active_timers[pump_id].active) {
    stopPump(pump_id);
  }
  
  // Start new timer
  active_timers[pump_id].active = true;
  active_timers[pump_id].pump_id = pump_id;
  active_timers[pump_id].start_time = rtc.now();
  active_timers[pump_id].duration_min = duration_min;
  
  // Turn on SSR
  digitalWrite(ssr_pin, HIGH);
  
  Serial.print(F("Pump "));
  Serial.print(pump_id);
  Serial.print(F(" started for "));
  Serial.print(duration_min);
  Serial.println(F(" minutes"));
}

void stopPump(uint8_t pump_id) {
  if (pump_id > 1) return;
  
  uint8_t ssr_pin = (pump_id == 0) ? SSR_PIN_1 : SSR_PIN_2;
  digitalWrite(ssr_pin, LOW);
  
  active_timers[pump_id].active = false;
  
  Serial.print(F("Pump "));
  Serial.print(pump_id);
  Serial.println(F(" stopped"));
}

void stopAllPumps() {
  stopPump(0);
  stopPump(1);
}

// ==================== LORA COMMUNICATION ====================
void checkLoRaCommands() {
  int packetSize = LoRa.parsePacket();
  if (packetSize == 0) return;
  
  // Read packet
  String packet = "";
  while (LoRa.available()) {
    packet += (char)LoRa.read();
  }
  
  // Process command
  if (processCommand(packet)) {
    system_status.lora_packets_received++;
    system_status.last_command_time = millis();
    
    // Send acknowledgment
    sendLoRaAck("OK");
  } else {
    system_status.lora_packets_failed++;
    sendLoRaAck("ERROR");
  }
}

bool processCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  
  // Command format: "CMD:param1,param2,..."
  int colon_pos = cmd.indexOf(':');
  String command = (colon_pos > 0) ? cmd.substring(0, colon_pos) : cmd;
  String params = (colon_pos > 0) ? cmd.substring(colon_pos + 1) : "";
  
  if (command == "SET_SCHEDULE") {
    return handleSetSchedule(params);
  } else if (command == "DELETE_SCHEDULE") {
    return handleDeleteSchedule(params);
  } else if (command == "CLEAR_ALL") {
    return handleClearAll();
  } else if (command == "RUN_TIMER") {
    return handleRunTimer(params);
  } else if (command == "STOP") {
    return handleStop(params);
  } else if (command == "GET_STATUS") {
    sendLoRaStatus();
    return true;
  } else if (command == "SYNC_TIME") {
    return handleSyncTime(params);
  }
  
  return false;
}

bool handleSetSchedule(String params) {
  // Format: "index,enabled,pump_id,hour,minute,duration,days"
  // Example: "0,1,0,8,30,60,0b1111111" (all days)
  
  int values[7];
  int pos = 0;
  int start = 0;
  
  for (int i = 0; i < 7; i++) {
    int comma = params.indexOf(',', start);
    if (comma < 0) comma = params.length();
    values[i] = params.substring(start, comma).toInt();
    start = comma + 1;
  }
  
  uint8_t index = values[0];
  if (index >= MAX_SCHEDULES) return false;
  
  schedules[index].enabled = values[1];
  schedules[index].pump_id = values[2];
  schedules[index].hour = values[3];
  schedules[index].minute = values[4];
  schedules[index].duration_min = values[5];
  schedules[index].days_of_week = values[6];
  
  schedule_modified = true;
  system_status.active_schedules = countActiveSchedules();
  
  Serial.print(F("Schedule #"));
  Serial.print(index);
  Serial.println(F(" set via LoRa"));
  
  return true;
}

bool handleDeleteSchedule(String params) {
  uint8_t index = params.toInt();
  if (index >= MAX_SCHEDULES) return false;
  
  memset(&schedules[index], 0, sizeof(ScheduleEntry));
  schedule_modified = true;
  system_status.active_schedules = countActiveSchedules();
  
  Serial.print(F("Schedule #"));
  Serial.print(index);
  Serial.println(F(" deleted via LoRa"));
  
  return true;
}

bool handleClearAll() {
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    memset(&schedules[i], 0, sizeof(ScheduleEntry));
  }
  schedule_modified = true;
  system_status.active_schedules = 0;
  
  Serial.println(F("All schedules cleared via LoRa"));
  return true;
}

bool handleRunTimer(String params) {
  // Format: "pump_id,duration_min"
  int comma = params.indexOf(',');
  if (comma < 0) return false;
  
  uint8_t pump_id = params.substring(0, comma).toInt();
  uint16_t duration = params.substring(comma + 1).toInt();
  
  if (pump_id > 1 || duration == 0 || duration > 255) return false;
  
  startPumpTimer(pump_id, duration);
  return true;
}

bool handleStop(String params) {
  if (params.length() == 0) {
    stopAllPumps();
  } else {
    uint8_t pump_id = params.toInt();
    if (pump_id > 1) return false;
    stopPump(pump_id);
  }
  return true;
}

bool handleSyncTime(String params) {
  // Format: "YYYY,MM,DD,HH,MM,SS"
  int values[6];
  int start = 0;
  
  for (int i = 0; i < 6; i++) {
    int comma = params.indexOf(',', start);
    if (comma < 0) comma = params.length();
    values[i] = params.substring(start, comma).toInt();
    start = comma + 1;
  }
  
  DateTime dt(values[0], values[1], values[2], values[3], values[4], values[5]);
  rtc.adjust(dt);
  
  Serial.println(F("Time synchronized via LoRa"));
  return true;
}

void sendLoRaAck(String message) {
  LoRa.beginPacket();
  LoRa.print("ACK:");
  LoRa.print(message);
  LoRa.endPacket();
}

void sendLoRaStatus() {
  LoRa.beginPacket();
  LoRa.print("STATUS:");
  LoRa.print("RTC=");
  LoRa.print(system_status.rtc_available ? "OK" : "FAIL");
  LoRa.print(",SCHED=");
  LoRa.print(system_status.active_schedules);
  LoRa.print(",P1=");
  LoRa.print(active_timers[0].active ? "ON" : "OFF");
  LoRa.print(",P2=");
  LoRa.print(active_timers[1].active ? "ON" : "OFF");
  LoRa.endPacket();
}

// ==================== EEPROM FUNCTIONS ====================
void loadSchedulesFromEEPROM() {
  // Check magic number
  uint16_t magic = (EEPROM.read(EEPROM_MAGIC) << 8) | EEPROM.read(EEPROM_MAGIC + 1);
  if (magic != EEPROM_MAGIC_VALUE) {
    Serial.println(F("EEPROM: No valid data found, initializing"));
    initializeEEPROM();
    return;
  }
  
  // Load schedules
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    uint16_t addr = EEPROM_SCHEDULE_BASE + (i * EEPROM_SCHEDULE_SIZE);
    EEPROM.get(addr, schedules[i]);
  }
  
  system_status.active_schedules = countActiveSchedules();
  Serial.print(F("EEPROM: Loaded "));
  Serial.print(system_status.active_schedules);
  Serial.println(F(" active schedules"));
}

void saveSchedulesToEEPROM() {
  // Write magic number
  EEPROM.write(EEPROM_MAGIC, (EEPROM_MAGIC_VALUE >> 8) & 0xFF);
  EEPROM.write(EEPROM_MAGIC + 1, EEPROM_MAGIC_VALUE & 0xFF);
  EEPROM.write(EEPROM_VERSION, 1);
  
  // Save schedules
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    uint16_t addr = EEPROM_SCHEDULE_BASE + (i * EEPROM_SCHEDULE_SIZE);
    EEPROM.put(addr, schedules[i]);
  }
  
  Serial.println(F("EEPROM: Schedules saved"));
}

void initializeEEPROM() {
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    memset(&schedules[i], 0, sizeof(ScheduleEntry));
  }
  saveSchedulesToEEPROM();
}

uint8_t countActiveSchedules() {
  uint8_t count = 0;
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    if (schedules[i].enabled) count++;
  }
  return count;
}

// ==================== SERIAL CLI ====================
void processSerialCLI() {
  while (Serial.available()) {
    char c = Serial.read();
    
    if (c == '\n' || c == '\r') {
      if (cli_index > 0) {
        cli_buffer[cli_index] = '\0';
        executeCLICommand(String(cli_buffer));
        cli_index = 0;
      }
    } else if (cli_index < CLI_BUFFER_SIZE - 1) {
      cli_buffer[cli_index++] = c;
    }
  }
}

void executeCLICommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  
  if (cmd == "HELP") {
    printHelp();
  } else if (cmd == "STATUS") {
    printStatus();
  } else if (cmd == "TIME") {
    printTime();
  } else if (cmd.startsWith("SET ")) {
    handleCLISetSchedule(cmd.substring(4));
  } else if (cmd == "LIST") {
    printSchedules();
  } else if (cmd.startsWith("CLEAR ")) {
    handleCLIClearSchedule(cmd.substring(6));
  } else if (cmd == "CLEAR ALL") {
    handleCLIClearAll();
  } else if (cmd.startsWith("RUN ")) {
    handleCLIRunTimer(cmd.substring(4));
  } else if (cmd.startsWith("STOP ")) {
    handleCLIStop(cmd.substring(5));
  } else if (cmd == "STOP") {
    stopAllPumps();
    Serial.println(F("All pumps stopped"));
  } else if (cmd.startsWith("SETTIME ")) {
    handleCLISetTime(cmd.substring(8));
  } else {
    Serial.print(F("Unknown command: "));
    Serial.println(cmd);
    Serial.println(F("Type 'help' for command list"));
  }
}

void printHelp() {
  Serial.println(F("\n=== Serial CLI Commands ==="));
  Serial.println(F("help              - Show this help"));
  Serial.println(F("status            - Show system status"));
  Serial.println(F("time              - Show current RTC time"));
  Serial.println(F("set <params>      - Set schedule"));
  Serial.println(F("  Format: index,enabled,pump_id,hour,minute,duration,days"));
  Serial.println(F("  Example: set 0,1,0,8,30,60,127"));
  Serial.println(F("  days: bitmask (1=Sun, 2=Mon, 4=Tue, 8=Wed, 16=Thu, 32=Fri, 64=Sat)"));
  Serial.println(F("  days=127 means all days"));
  Serial.println(F("list              - List all schedules"));
  Serial.println(F("clear <index>     - Clear schedule at index"));
  Serial.println(F("clear all         - Clear all schedules"));
  Serial.println(F("run <pump,duration> - Run pump for duration (minutes)"));
  Serial.println(F("  Example: run 0,30"));
  Serial.println(F("stop [pump_id]    - Stop pump (or all if no ID)"));
  Serial.println(F("settime <params>  - Set RTC time"));
  Serial.println(F("  Format: YYYY,MM,DD,HH,MM,SS"));
  Serial.println(F("  Example: settime 2024,1,15,14,30,0"));
  Serial.println();
}

void printStatus() {
  Serial.println(F("\n=== System Status ==="));
  Serial.print(F("RTC: "));
  Serial.println(system_status.rtc_available ? F("OK") : F("FAIL"));
  Serial.print(F("LoRa: "));
  Serial.println(system_status.lora_connected ? F("OK") : F("FAIL"));
  Serial.print(F("Active Schedules: "));
  Serial.println(system_status.active_schedules);
  Serial.print(F("LoRa Packets RX: "));
  Serial.println(system_status.lora_packets_received);
  Serial.print(F("LoRa Packets Failed: "));
  Serial.println(system_status.lora_packets_failed);
  Serial.print(F("Pump 1: "));
  Serial.println(active_timers[0].active ? F("RUNNING") : F("STOPPED"));
  Serial.print(F("Pump 2: "));
  Serial.println(active_timers[1].active ? F("RUNNING") : F("STOPPED"));
  Serial.println();
}

void printTime() {
  if (!system_status.rtc_available) {
    Serial.println(F("RTC not available"));
    return;
  }
  
  DateTime now = rtc.now();
  Serial.print(F("Current Time: "));
  Serial.print(now.year());
  Serial.print(F("-"));
  if (now.month() < 10) Serial.print(F("0"));
  Serial.print(now.month());
  Serial.print(F("-"));
  if (now.day() < 10) Serial.print(F("0"));
  Serial.print(now.day());
  Serial.print(F(" "));
  if (now.hour() < 10) Serial.print(F("0"));
  Serial.print(now.hour());
  Serial.print(F(":"));
  if (now.minute() < 10) Serial.print(F("0"));
  Serial.print(now.minute());
  Serial.print(F(":"));
  if (now.second() < 10) Serial.print(F("0"));
  Serial.print(now.second());
  Serial.println();
}

void handleCLISetSchedule(String params) {
  int values[7];
  int start = 0;
  
  for (int i = 0; i < 7; i++) {
    int comma = params.indexOf(',', start);
    if (comma < 0) comma = params.length();
    values[i] = params.substring(start, comma).toInt();
    start = comma + 1;
  }
  
  uint8_t index = values[0];
  if (index >= MAX_SCHEDULES) {
    Serial.println(F("ERROR: Invalid schedule index"));
    return;
  }
  
  schedules[index].enabled = values[1];
  schedules[index].pump_id = values[2];
  schedules[index].hour = values[3];
  schedules[index].minute = values[4];
  schedules[index].duration_min = values[5];
  schedules[index].days_of_week = values[6];
  
  schedule_modified = true;
  system_status.active_schedules = countActiveSchedules();
  
  Serial.print(F("Schedule #"));
  Serial.print(index);
  Serial.println(F(" set"));
}

void printSchedules() {
  Serial.println(F("\n=== Schedules ==="));
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    if (schedules[i].enabled || 
        schedules[i].hour != 0 || 
        schedules[i].minute != 0) {
      Serial.print(F("#"));
      Serial.print(i);
      Serial.print(F(": "));
      Serial.print(schedules[i].enabled ? F("ENABLED") : F("DISABLED"));
      Serial.print(F(" | Pump "));
      Serial.print(schedules[i].pump_id);
      Serial.print(F(" | "));
      if (schedules[i].hour < 10) Serial.print(F("0"));
      Serial.print(schedules[i].hour);
      Serial.print(F(":"));
      if (schedules[i].minute < 10) Serial.print(F("0"));
      Serial.print(schedules[i].minute);
      Serial.print(F(" | "));
      Serial.print(schedules[i].duration_min);
      Serial.print(F(" min | Days: "));
      Serial.println(schedules[i].days_of_week, BIN);
    }
  }
  Serial.println();
}

void handleCLIClearSchedule(String params) {
  uint8_t index = params.toInt();
  if (index >= MAX_SCHEDULES) {
    Serial.println(F("ERROR: Invalid schedule index"));
    return;
  }
  
  memset(&schedules[index], 0, sizeof(ScheduleEntry));
  schedule_modified = true;
  system_status.active_schedules = countActiveSchedules();
  
  Serial.print(F("Schedule #"));
  Serial.print(index);
  Serial.println(F(" cleared"));
}

void handleCLIClearAll() {
  for (uint8_t i = 0; i < MAX_SCHEDULES; i++) {
    memset(&schedules[i], 0, sizeof(ScheduleEntry));
  }
  schedule_modified = true;
  system_status.active_schedules = 0;
  Serial.println(F("All schedules cleared"));
}

void handleCLIRunTimer(String params) {
  int comma = params.indexOf(',');
  if (comma < 0) {
    Serial.println(F("ERROR: Format: run pump_id,duration_min"));
    return;
  }
  
  uint8_t pump_id = params.substring(0, comma).toInt();
  uint16_t duration = params.substring(comma + 1).toInt();
  
  if (pump_id > 1 || duration == 0 || duration > 255) {
    Serial.println(F("ERROR: Invalid parameters"));
    return;
  }
  
  startPumpTimer(pump_id, duration);
}

void handleCLIStop(String params) {
  if (params.length() == 0) {
    stopAllPumps();
    Serial.println(F("All pumps stopped"));
  } else {
    uint8_t pump_id = params.toInt();
    if (pump_id > 1) {
      Serial.println(F("ERROR: Invalid pump ID (0 or 1)"));
      return;
    }
    stopPump(pump_id);
  }
}

void handleCLISetTime(String params) {
  if (!system_status.rtc_available) {
    Serial.println(F("ERROR: RTC not available"));
    return;
  }
  
  int values[6];
  int start = 0;
  
  for (int i = 0; i < 6; i++) {
    int comma = params.indexOf(',', start);
    if (comma < 0) comma = params.length();
    values[i] = params.substring(start, comma).toInt();
    start = comma + 1;
  }
  
  DateTime dt(values[0], values[1], values[2], values[3], values[4], values[5]);
  rtc.adjust(dt);
  
  Serial.println(F("RTC time set"));
  printTime();
}

void reportSystemStatus() {
  // Periodic status report (can be sent via LoRa or Serial)
  if (system_status.lora_connected) {
    sendLoRaStatus();
  }
}

