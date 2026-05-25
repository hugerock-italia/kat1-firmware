/*
 * HUGEROCK DISCOVERY PAD - FIRMWARE v03
 * Joystick Hall SMC25 + BLE OTA (no WiFi) + colori mappa via app
 * Autore: ANDREA BRENTEGANI
 * Board: Xiao ESP32-S3
 *
 * Differenze rispetto a v02:
 *  - Pin LED/IO aggiornati per Xiao (D-notation, LED common cathode → no inversione)
 *  - Colori mappa configurabili via BLE e salvati in NVS (Preferences)
 *  - Joystick: aggiunto SATURATION_LOW per soglia inferiore simmetrica
 *
 * BUG FIX rispetto alla versione corrotta dall'agente:
 *  - configCharacteristic: WRITE|READ (non NOTIFY): evita overflow GATT table (~60 attr)
 *  - Rimosso delay(1000) dal callback BLE (causava crash del BLE stack)
 *  - Ripristinato colore() nel loop (non setColor diretto)
 *  - Ordine setup(): USB.begin() prima di setupBLE() (richiesto su Xiao ESP32-S3)
 *  - HID descriptor: ripristinato da v02 (testato e funzionante)
 *  - RESET:CONFIG ora resetta anche le Preferences NVS
 */

// =====================================================================
//  KAT-ADV Firmware  ·  DISCOVERY (joystick Hall)
//  Version: 0.4.1  ·  Build date: 2026-05-24
//  Repo: hugerock-italia/kat1-firmware
// =====================================================================

#include <USB.h>
#include <USBHIDKeyboard.h>
#include <NimBLEDevice.h>
#include <NimBLEServer.h>
#include <NimBLEHIDDevice.h>
#include <NimBLE2904.h>
#include <nvs_flash.h>
#include <Keypad.h>
#include <EEPROM.h>
#include <Update.h>
#include <Preferences.h>

// ==================== ISTANZE GLOBALI ====================

USBHIDKeyboard usbKeyboard;
NimBLEServer* pServer = nullptr;
NimBLEHIDDevice* hid = nullptr;
NimBLECharacteristic* inputKeyboard   = nullptr;
NimBLECharacteristic* inputVolumeUp   = nullptr;
NimBLECharacteristic* inputVolumeDown = nullptr;
NimBLECharacteristic* inputNextTrack  = nullptr;
NimBLECharacteristic* inputPrevTrack  = nullptr;
NimBLECharacteristic* inputPlayPause  = nullptr;
NimBLECharacteristic* inputMute       = nullptr;
NimBLECharacteristic* configCharacteristic = nullptr;
NimBLECharacteristic* otaCharacteristic    = nullptr;
bool bleConnected    = false;
bool configModeActive = false;

// ==================== OTA STATE ====================

bool otaBleActive    = false;
size_t otaExpectedSize = 0;
size_t otaWritten      = 0;

// ==================== PIN (Xiao ESP32-S3) ====================

const int LED_RED     = D7;
const int LED_GREEN   = D8;
const int LED_BLUE    = D9;
const int Vpin        = D10;
const int PIN_X       = D5;
const int PIN_Y       = D4;
const int EXT_BTN_PIN = D6;

// ==================== JOYSTICK ====================

const int JOYSTICK_CENTER    = 1920;
const int DEADZONE_POSITIVE  = 200;
const int DEADZONE_NEGATIVE  = 200;
const int THRESHOLD_UP       = 2420;
const int THRESHOLD_DOWN     = 1420;
const int THRESHOLD_LEFT     = 1420;
const int THRESHOLD_RIGHT    = 2420;
const int SATURATION_LIMIT   = 3800;
const int SATURATION_LOW     = 40;   // v03: soglia inferiore simmetrica
const int JOYSTICK_MAX_DELAY = 400;
const int JOYSTICK_MIN_DELAY = 80;

int lastJoystickState = 0;
unsigned long lastJoystickMove = 0;
int lockedDirection = 0;
unsigned long directionLockTime = 0;
const int DIRECTION_LOCK_TIMEOUT = 300;

bool extBtnLast = HIGH;
unsigned long extBtnLastTime = 0;
const unsigned long debounceDelay = 50;

// ==================== VELOCITÀ ====================

int vel  = LOW;
int Mvel = 0;
const int Vel_ab = 1;

// ==================== LED ====================

// LEDC channels — core 2.x richiede canali espliciti (core 3.x li gestisce in automatico)
#define LEDC_CH_RED   0
#define LEDC_CH_GREEN 1
#define LEDC_CH_BLUE  2

unsigned long intervalloLampeggio = 3000;
float dutyCycle = 0.04;
bool firstConn  = false;

// Colori RGB per ciascuna mappa — caricati da NVS, default giallo/blu/verde
uint8_t mapColor[3][3] = { {255,255,0}, {0,0,255}, {0,255,0} };
Preferences prefs;

// Preview temporaneo colore dopo CONFIG:MAP_COLOR (gestito nel loop, MAI nel callback BLE)
bool showColorPreview     = false;
unsigned long colorPreviewTime = 0;
uint8_t previewColor[3]   = {0,0,0};

// ==================== MATRICE ====================

const byte ROWS = 2;
const byte COLS = 2;
char keys[ROWS][COLS] = { {'1','2'}, {'3','4'} };
byte rowPins[ROWS] = { 1, 2 };
byte colPins[COLS] = { 11, 12 };

// ==================== MAPPE ====================

#define EEPROM_KEYMAP_ADDR      0
#define EEPROM_KEYASSIGN_BASE   0x10
#define EEPROM_REPEATFLAGS_BASE 0x50
#define EEPROM_WHEELFLAGS_BASE  0x80
#define KEYS_PER_MAP  14
#define KEYMAP_COUNT   3

int current_keymap = 1;
int key_mem        = 1;

uint8_t repeatFlags[KEYS_PER_MAP];

uint8_t defaultRepeatFlags[KEYMAP_COUNT][KEYS_PER_MAP] = {
  { 1,1,1,1, 0,0,0,0, 0,0,0,0, 0,0 },
  { 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0 },
  { 1,1,1,1, 0,0,0,0, 0,0,0,0, 0,0 }
};

uint8_t defaultCommands[KEYMAP_COUNT][KEYS_PER_MAP] = {
  { 0x24, 0x24, 0x25, 0x25, 0x06, 0x07, 0x09, 0x2C, 0x52, 0x51, 0x50, 0x4F, 0x06, 0x06 },  // Map1 OsmAnd  ext=C wheel=C
  { 0x4F, 0x4F, 0x50, 0x50, 0x1D, 0x15, 0x51, 0x2C, 0x52, 0x51, 0x26, 0x27, 0x10, 0x0A },  // Map2 TerraPirata ext=M wheel=G
  { 0x52, 0x52, 0x51, 0x51, 0x52, 0x07, 0x51, 0x1D, 0x24, 0x25, 0x25, 0x24, 0x17, 0x17 }   // Map3 MRB     ext=T wheel=T
};

uint8_t currentAssignments[KEYS_PER_MAP];
uint8_t wheelEnabled[KEYMAP_COUNT] = {1,1,1};

// ==================== TEMPORIZZAZIONI ====================

unsigned long prevMillis = 0;
const long interval = 40;
unsigned long prevMilLP = 0;
int last_keypad_state   = IDLE;
const int long_press_time            = 440;
const int long_press_repeat_interval = 80;

Keypad TRAX = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

unsigned long lastKeymapChange = 0;
const int keymapChangeDelay    = 500;
bool key1Detected = false;
bool key2Detected = false;
unsigned long detectionWindow  = 0;
const int maxDetectionWindow   = 1000;
bool firstClickCompleted       = false;
unsigned long firstClickTime   = 0;
bool firstClickActive          = false;
const int doubleClickWindow    = 800;

// ==================== PROTOTIPI ====================

void setupBLE();
void setOptimalConnectionParams();
void send_key_dual(uint8_t key);
void send_volume_up_dual();
void send_volume_down_dual();
void send_next_track_dual();
void send_previous_track_dual();
void send_play_pause_dual();
void send_mute_dual();
void send_alt_tab_tab_dual();
void send_repeating_key(uint8_t cmdId);
void keypad_handler(KeypadEvent key);
int  is_key_repeating(char key_pressed);
void checkKeymapChange();
void performKeymapChange(unsigned long currentTime);
void handle_joystick();
void colore(unsigned char r, unsigned char g, unsigned char b);
void setColor(uint8_t r, uint8_t g, uint8_t b);
void loadAssignmentsFromEEPROM(int mapId);
void loadRepeatFlagsFromEEPROM(int mapId);
void loadWheelFlagsFromEEPROM();
void executeCommandById(uint8_t cmdId);
void loadMapColors();
void saveMapColor(int mapIndex, uint8_t r, uint8_t g, uint8_t b);

// ==================== HID REPORT DESCRIPTOR (da v02, testato) ====================

static const uint8_t hidReportDescriptor[] = {
  0x05,0x01,0x09,0x06,0xA1,0x01,0x85,0x01,
  0x05,0x07,0x19,0xE0,0x29,0xE7,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x08,0x81,0x02,
  0x95,0x01,0x75,0x08,0x81,0x03,
  0x95,0x06,0x75,0x08,0x15,0x00,0x25,0x73,0x05,0x07,0x19,0x00,0x29,0x73,0x81,0x00,0xC0,
  0x05,0x0C,0x09,0x01,0xA1,0x01,0x85,0x02,0x15,0x00,0x25,0x01,0x75,0x08,0x95,0x01,0x0A,0xEA,0x00,0x81,0x02,0xC0,
  0x05,0x0C,0x09,0x01,0xA1,0x01,0x85,0x03,0x15,0x00,0x25,0x01,0x75,0x08,0x95,0x01,0x0A,0xE9,0x00,0x81,0x02,0xC0,
  0x05,0x0C,0x09,0x01,0xA1,0x01,0x85,0x04,0x15,0x00,0x25,0x01,0x75,0x08,0x95,0x01,0x0A,0xB5,0x00,0x81,0x02,0xC0,
  0x05,0x0C,0x09,0x01,0xA1,0x01,0x85,0x05,0x15,0x00,0x25,0x01,0x75,0x08,0x95,0x01,0x0A,0xB6,0x00,0x81,0x02,0xC0,
  0x05,0x0C,0x09,0x01,0xA1,0x01,0x85,0x06,0x15,0x00,0x25,0x01,0x75,0x08,0x95,0x01,0x0A,0xCD,0x00,0x81,0x02,0xC0,
  0x05,0x0C,0x09,0x01,0xA1,0x01,0x85,0x07,0x15,0x00,0x25,0x01,0x75,0x08,0x95,0x01,0x0A,0xE2,0x00,0x81,0x02,0xC0
};

// ==================== CALLBACK CONFIG ====================

class ConfigCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) {
    std::string value = pCharacteristic->getValue();
    if (value.length() == 0) return;
    String cmd(value.c_str());
    Serial.print("CMD: "); Serial.println(cmd);

    if (cmd == "CONFIG:MODE:ON")  { configModeActive = true;  return; }
    if (cmd == "CONFIG:MODE:OFF") { configModeActive = false; return; }

    if (cmd == "RESET:CONFIG") {
      EEPROM.begin(512);
      for (int i = 0; i < 512; i++) EEPROM.write(i, 0xFF);
      EEPROM.commit();
      // Reset anche colori NVS
      prefs.begin("mapcolors", false);
      prefs.clear();
      prefs.end();
      mapColor[0][0]=255; mapColor[0][1]=255; mapColor[0][2]=0;
      mapColor[1][0]=0;   mapColor[1][1]=0;   mapColor[1][2]=255;
      mapColor[2][0]=0;   mapColor[2][1]=255; mapColor[2][2]=0;
      return;
    }

    if (cmd.startsWith("CONFIG:WHEEL:")) {
      int c1 = cmd.indexOf(':', 13);
      int mapId = cmd.substring(13, c1).toInt();
      int enable = cmd.substring(c1 + 1).toInt();
      if (mapId >= 0 && mapId < KEYMAP_COUNT) {
        wheelEnabled[mapId] = enable ? 1 : 0;
        EEPROM.write(EEPROM_WHEELFLAGS_BASE + mapId, wheelEnabled[mapId]);
        EEPROM.commit();
      }
      return;
    }

    // Imposta colore di una mappa: CONFIG:MAP_COLOR:<mappa1-3>:<R>:<G>:<B>
    // NON chiamare delay() o setColor() qui — siamo in un callback BLE.
    // Il preview visivo è delegato al loop() tramite flag.
    if (cmd.startsWith("CONFIG:MAP_COLOR:")) {
      int p1 = cmd.indexOf(':', 17);
      int p2 = cmd.indexOf(':', p1 + 1);
      int p3 = cmd.indexOf(':', p2 + 1);
      int mapN = cmd.substring(17, p1).toInt();
      uint8_t r = (uint8_t)cmd.substring(p1 + 1, p2).toInt();
      uint8_t g = (uint8_t)cmd.substring(p2 + 1, p3).toInt();
      uint8_t b = (uint8_t)cmd.substring(p3 + 1).toInt();
      if (mapN >= 1 && mapN <= 3) {
        saveMapColor(mapN - 1, r, g, b);
        previewColor[0] = r;
        previewColor[1] = g;
        previewColor[2] = b;
        showColorPreview  = true;
        colorPreviewTime  = millis();
      }
      return;
    }

    // Query colori attuali: CONFIG:GET_COLORS
    // Risposta scritta nella caratteristica (READ da app, no NOTIFY)
    if (cmd == "CONFIG:GET_COLORS") {
      String resp = String(mapColor[0][0])+","+mapColor[0][1]+","+mapColor[0][2]+";"+
                    mapColor[1][0]+","+mapColor[1][1]+","+mapColor[1][2]+";"+
                    mapColor[2][0]+","+mapColor[2][1]+","+mapColor[2][2];
      configCharacteristic->setValue(resp.c_str());
      // Il client legge il valore con una READ — niente notify() per non aggiungere CCCD
      return;
    }

    if (cmd.startsWith("CONFIG:OTA:BEGIN:")) {
      otaExpectedSize = cmd.substring(17).toInt();
      otaWritten = 0;
      if (Update.begin(otaExpectedSize)) {
        otaBleActive = true;
        setColor(255,255,255);
        Serial.printf("OTA BEGIN size=%u\n", otaExpectedSize);
      } else {
        String err = "ERR:BEGIN";
        otaCharacteristic->setValue(err.c_str());
        otaCharacteristic->notify();
      }
      return;
    }

    if (cmd == "CONFIG:OTA:END") {
      if (otaBleActive) {
        if (Update.end(true)) {
          Serial.println("OTA OK - restart");
          String ok = "OTA:OK";
          otaCharacteristic->setValue(ok.c_str());
          otaCharacteristic->notify();
          delay(500);
          ESP.restart();
        } else {
          otaBleActive = false;
          String err = "ERR:END";
          otaCharacteristic->setValue(err.c_str());
          otaCharacteristic->notify();
        }
      }
      return;
    }

    if (cmd.startsWith("CONFIG:")) {
      int mapId=0,keyIndex=0,commandId=0,repeat=0,partIdx=0,lastIdx=7;
      for (int i=7; i<=(int)cmd.length() && partIdx<4; i++) {
        if (cmd[i]==':'||i==(int)cmd.length()) {
          String part=cmd.substring(lastIdx,i);
          if      (partIdx==0) mapId     = part.toInt();
          else if (partIdx==1) keyIndex  = part.toInt();
          else if (partIdx==2) commandId = (int)strtol(part.c_str(),NULL,16);
          else if (partIdx==3) repeat    = part.toInt();
          partIdx++; lastIdx=i+1;
        }
      }
      if (mapId>=0&&mapId<KEYMAP_COUNT&&keyIndex>=0&&keyIndex<KEYS_PER_MAP) {
        EEPROM.write(EEPROM_KEYASSIGN_BASE+(mapId*KEYS_PER_MAP)+keyIndex,(uint8_t)commandId);
        EEPROM.write(EEPROM_REPEATFLAGS_BASE+(mapId*KEYS_PER_MAP)+keyIndex,(uint8_t)(repeat?1:0));
        EEPROM.commit();
        if (mapId==current_keymap-1) {
          loadAssignmentsFromEEPROM(current_keymap);
          loadRepeatFlagsFromEEPROM(current_keymap);
        }
      }
    }
  }
};

// ==================== CALLBACK OTA ====================

class OtaCharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* pCharacteristic) {
    if (!otaBleActive) return;
    std::string data = pCharacteristic->getValue();
    if (data.length()==0) return;
    size_t written = Update.write((uint8_t*)data.data(), data.length());
    otaWritten += written;
    if (written!=data.length()) {
      otaBleActive=false;
      String err="ERR:WRITE"; otaCharacteristic->setValue(err.c_str()); otaCharacteristic->notify();
      return;
    }
    if (otaWritten%4096<512) {
      String prog="PROG:"+String(otaWritten);
      otaCharacteristic->setValue(prog.c_str()); otaCharacteristic->notify();
    }
  }
};

// ==================== CALLBACK SERVER ====================

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer* pServer) {
    bleConnected = true;
    Serial.println("BLE Connected");
  }
  void onDisconnect(NimBLEServer* pServer) {
    bleConnected = false;
    otaBleActive = false;
    Serial.println("BLE Disconnected");
    vTaskDelay(pdMS_TO_TICKS(1000));
    NimBLEDevice::startAdvertising();
  }
};

// ==================== SETUP BLE (identico a v02) ====================

void setupBLE() {
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_flash_init();
  }

  NimBLEDevice::init("DISCOVERY-PAD");
  NimBLEDevice::setSecurityAuth(true, true, true);
  NimBLEDevice::setSecurityPasskey(123456);
  NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9);
  NimBLEDevice::setMTU(515);

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  hid = new NimBLEHIDDevice(pServer);
  inputKeyboard   = hid->inputReport(1);
  inputVolumeUp   = hid->inputReport(2);
  inputVolumeDown = hid->inputReport(3);
  inputNextTrack  = hid->inputReport(4);
  inputPrevTrack  = hid->inputReport(5);
  inputPlayPause  = hid->inputReport(6);
  inputMute       = hid->inputReport(7);
  hid->manufacturer()->setValue("HUGEROCK");
  hid->reportMap((uint8_t*)hidReportDescriptor, sizeof(hidReportDescriptor));
  hid->startServices();

  NimBLEService* configService = pServer->createService("12345678-1234-1234-1234-123456789012");

  // WRITE | READ: niente NOTIFY per non aggiungere CCCD e sforare il limite GATT (~60 attributi)
  // L'app legge la risposta di CONFIG:GET_COLORS con una READ esplicita
  configCharacteristic = configService->createCharacteristic(
    "87654321-4321-4321-4321-210987654321",
    NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::READ);
  configCharacteristic->setCallbacks(new ConfigCharacteristicCallbacks());

  otaCharacteristic = configService->createCharacteristic(
    "87654321-4321-4321-4321-210987654323",
    NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
  otaCharacteristic->setCallbacks(new OtaCharacteristicCallbacks());

  configService->start();

  NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
  pAdv->setAppearance(HID_KEYBOARD);
  pAdv->addServiceUUID(hid->hidService()->getUUID());
  pAdv->addServiceUUID(configService->getUUID());
  pAdv->setMinInterval(32);
  pAdv->setMaxInterval(144);
  pAdv->setScanResponse(true);
  pAdv->start();

  Serial.println("BLE ready");
}

void setOptimalConnectionParams() {
  if (bleConnected && pServer != nullptr) NimBLEDevice::setPower(ESP_PWR_LVL_P9);
}

// ==================== EEPROM ====================

void loadAssignmentsFromEEPROM(int mapId) {
  if (mapId<1||mapId>KEYMAP_COUNT) mapId=1;
  uint16_t base = EEPROM_KEYASSIGN_BASE+((mapId-1)*KEYS_PER_MAP);
  for (int i=0;i<KEYS_PER_MAP;i++) {
    uint8_t v=EEPROM.read(base+i);
    currentAssignments[i]=(v==0xFF)?defaultCommands[mapId-1][i]:v;
  }
}

void loadRepeatFlagsFromEEPROM(int mapId) {
  if (mapId<1||mapId>KEYMAP_COUNT) mapId=1;
  uint16_t base=EEPROM_REPEATFLAGS_BASE+((mapId-1)*KEYS_PER_MAP);
  for (int i=0;i<KEYS_PER_MAP;i++) {
    uint8_t v=EEPROM.read(base+i);
    repeatFlags[i]=(v==0xFF)?defaultRepeatFlags[mapId-1][i]:v;
  }
}

void loadWheelFlagsFromEEPROM() {
  for (int i=0;i<KEYMAP_COUNT;i++) {
    uint8_t v=EEPROM.read(EEPROM_WHEELFLAGS_BASE+i);
    wheelEnabled[i]=(v==0xFF)?1:v;
  }
}

// ==================== COLORI MAPPA (NVS) ====================

void loadMapColors() {
  if (!prefs.begin("mapcolors", false)) return;  // false: crea il namespace se assente
  mapColor[0][0]=prefs.getUChar("m1r",255); mapColor[0][1]=prefs.getUChar("m1g",255); mapColor[0][2]=prefs.getUChar("m1b",0);
  mapColor[1][0]=prefs.getUChar("m2r",0);   mapColor[1][1]=prefs.getUChar("m2g",0);   mapColor[1][2]=prefs.getUChar("m2b",255);
  mapColor[2][0]=prefs.getUChar("m3r",0);   mapColor[2][1]=prefs.getUChar("m3g",255); mapColor[2][2]=prefs.getUChar("m3b",0);
  prefs.end();
}

void saveMapColor(int mapIndex, uint8_t r, uint8_t g, uint8_t b) {
  if (mapIndex<0||mapIndex>2) return;
  mapColor[mapIndex][0]=r; mapColor[mapIndex][1]=g; mapColor[mapIndex][2]=b;
  prefs.begin("mapcolors", false);
  String prefix = "m"+String(mapIndex+1);
  prefs.putUChar((prefix+"r").c_str(), r);
  prefs.putUChar((prefix+"g").c_str(), g);
  prefs.putUChar((prefix+"b").c_str(), b);
  prefs.end();
}

// ==================== EXECUTE COMMAND ====================

void executeCommandById(uint8_t cmdId) {
  switch (cmdId) {
    case 0x00: return;
    case 0x24: send_volume_up_dual();      break;
    case 0x25: send_volume_down_dual();    break;
    case 0x26: send_next_track_dual();     break;
    case 0x27: send_previous_track_dual(); break;
    case 0x2C: send_alt_tab_tab_dual();    break;
    case 0x28: case 0x52: send_key_dual(KEY_UP_ARROW);    break;
    case 0x29: case 0x51: send_key_dual(KEY_DOWN_ARROW);  break;
    case 0x2A: case 0x50: send_key_dual(KEY_LEFT_ARROW);  break;
    case 0x2B: case 0x4F: send_key_dual(KEY_RIGHT_ARROW); break;
    case 0x4B: send_key_dual(KEY_PAGE_UP);   break;
    case 0x4E: send_key_dual(KEY_PAGE_DOWN); break;
    case 0x38: send_key_dual('+'); break;
    case 0x39: send_key_dual('-'); break;
    case 0x2D: send_play_pause_dual(); break;
    case 0x2E: send_mute_dual();       break;
    default: {
      char c='a';
      if      (cmdId>=0x04&&cmdId<=0x1D) c='a'+(cmdId-0x04);
      else if (cmdId>=0x1E&&cmdId<=0x26) c=(cmdId==0x27)?'0':('1'+(cmdId-0x1E));
      send_key_dual(c);
    }
  }
}

// ==================== SEND FUNCTIONS ====================

void send_key_dual(uint8_t key) {
  if (bleConnected && inputKeyboard) {
    uint8_t report[8]={0};
    switch (key) {
      case '+':             report[0]=0x02; report[2]=0x2E; break;
      case '-':             report[2]=0x2D; break;
      case KEY_UP_ARROW:    report[2]=0x52; break;
      case KEY_DOWN_ARROW:  report[2]=0x51; break;
      case KEY_LEFT_ARROW:  report[2]=0x50; break;
      case KEY_RIGHT_ARROW: report[2]=0x4F; break;
      case KEY_PAGE_UP:     report[2]=0x4B; break;
      case KEY_PAGE_DOWN:   report[2]=0x4E; break;
      default:
        if      (key>='a'&&key<='z') report[2]=key-'a'+0x04;
        else if (key>='A'&&key<='Z') { report[0]=0x02; report[2]=key-'A'+0x04; }
        else if (key>='1'&&key<='9') report[2]=key-'1'+0x1E;
        else if (key=='0')           report[2]=0x27;
        break;
    }
    inputKeyboard->setValue(report,sizeof(report)); inputKeyboard->notify();
    delay(5);
    memset(report,0,sizeof(report));
    inputKeyboard->setValue(report,sizeof(report)); inputKeyboard->notify();
  }
  if      (key=='+')           { usbKeyboard.press(KEY_LEFT_SHIFT); usbKeyboard.press('='); usbKeyboard.releaseAll(); }
  else if (key=='-')           { usbKeyboard.press('-'); usbKeyboard.release('-'); }
  else if (key==KEY_PAGE_UP)   { usbKeyboard.press(KEY_PAGE_UP);   usbKeyboard.release(KEY_PAGE_UP); }
  else if (key==KEY_PAGE_DOWN) { usbKeyboard.press(KEY_PAGE_DOWN); usbKeyboard.release(KEY_PAGE_DOWN); }
  else usbKeyboard.write(key);
}

void send_volume_up_dual() {
  if (!bleConnected||!inputVolumeUp) return;
  uint8_t r[1]={0x01}; inputVolumeUp->setValue(r,1); inputVolumeUp->notify();
  delay(5); r[0]=0; inputVolumeUp->setValue(r,1); inputVolumeUp->notify();
}
void send_volume_down_dual() {
  if (!bleConnected||!inputVolumeDown) return;
  uint8_t r[1]={0x01}; inputVolumeDown->setValue(r,1); inputVolumeDown->notify();
  delay(5); r[0]=0; inputVolumeDown->setValue(r,1); inputVolumeDown->notify();
}
void send_next_track_dual() {
  if (!bleConnected||!inputNextTrack) return;
  uint8_t r[1]={0x01}; inputNextTrack->setValue(r,1); inputNextTrack->notify();
  delay(5); r[0]=0; inputNextTrack->setValue(r,1); inputNextTrack->notify();
}
void send_previous_track_dual() {
  if (!bleConnected||!inputPrevTrack) return;
  uint8_t r[1]={0x01}; inputPrevTrack->setValue(r,1); inputPrevTrack->notify();
  delay(5); r[0]=0; inputPrevTrack->setValue(r,1); inputPrevTrack->notify();
}
void send_play_pause_dual() {
  if (!bleConnected||!inputPlayPause) return;
  uint8_t r[1]={0x01}; inputPlayPause->setValue(r,1); inputPlayPause->notify();
  delay(5); r[0]=0; inputPlayPause->setValue(r,1); inputPlayPause->notify();
}
void send_mute_dual() {
  if (!bleConnected||!inputMute) return;
  uint8_t r[1]={0x01}; inputMute->setValue(r,1); inputMute->notify();
  delay(5); r[0]=0; inputMute->setValue(r,1); inputMute->notify();
}
void send_alt_tab_tab_dual() {
  if (bleConnected && inputKeyboard) {
    uint8_t report[8]={0};
    report[0]=0x04; inputKeyboard->setValue(report,8); inputKeyboard->notify(); delay(200);
    report[2]=0x2B; inputKeyboard->setValue(report,8); inputKeyboard->notify(); delay(100);
    report[2]=0x00; inputKeyboard->setValue(report,8); inputKeyboard->notify(); delay(1000);
    memset(report,0,8); inputKeyboard->setValue(report,8); inputKeyboard->notify();
  }
  usbKeyboard.press(KEY_LEFT_ALT); delay(500);
  usbKeyboard.press(KEY_TAB); delay(50);
  usbKeyboard.release(KEY_TAB); delay(1000);
  usbKeyboard.releaseAll();
}

void send_repeating_key(uint8_t cmdId) {
  while (TRAX.getState()==HOLD) {
    unsigned long cur=millis();
    if (cur-prevMilLP>=long_press_repeat_interval) { prevMilLP=cur; executeCommandById(cmdId); }
    TRAX.getKey();
  }
}

int is_key_repeating(char key_pressed) {
  int idx=-1;
  switch (key_pressed) { case '1':idx=0;break; case '2':idx=2;break; case '3':idx=4;break; case '4':idx=6;break; }
  return (idx>=0)?repeatFlags[idx]:0;
}

void send_short_press(KeypadEvent key) {
  int idx=-1;
  switch (key) { case '1':idx=0;break; case '2':idx=2;break; case '3':idx=4;break; case '4':idx=6;break; }
  if (idx>=0) executeCommandById(currentAssignments[idx]);
}

void send_long_press(KeypadEvent key) {
  int idx=-1;
  switch (key) { case '1':idx=1;break; case '2':idx=3;break; case '3':idx=5;break; case '4':idx=7;break; }
  if (idx>=0) {
    if (is_key_repeating(key)) send_repeating_key(currentAssignments[idx]);
    else                       executeCommandById(currentAssignments[idx]);
  }
}

void keypad_handler(KeypadEvent key) {
  if (otaBleActive) return;
  switch (TRAX.getState()) {
    case PRESSED:  if (is_key_repeating(key)) send_short_press(key); break;
    case HOLD:     send_long_press(key); break;
    case RELEASED: if (last_keypad_state==PRESSED&&!is_key_repeating(key)) send_short_press(key); break;
  }
  last_keypad_state=TRAX.getState();
}

// ==================== JOYSTICK ====================

void handle_joystick() {
  if (otaBleActive) return;
  int x=analogRead(PIN_X), y=analogRead(PIN_Y);
  unsigned long now=millis();
  int dx=x-JOYSTICK_CENTER, dy=y-JOYSTICK_CENTER;
  int absDx=abs(dx), absDy=abs(dy);

  if (absDx<DEADZONE_POSITIVE&&absDy<DEADZONE_POSITIVE) {
    lastJoystickState=0; lockedDirection=0; directionLockTime=0; return;
  }
  if (lockedDirection!=0&&(now-directionLockTime>DIRECTION_LOCK_TIMEOUT)) lockedDirection=0;

  bool moveDetected=false; float amplitude=0; int direction=0;

  if (lockedDirection!=0) {
    direction=lockedDirection;
    switch (direction) {
      case 1: if (y>THRESHOLD_UP)    { amplitude=min((float)(y-THRESHOLD_UP)/(float)(SATURATION_LIMIT-THRESHOLD_UP),1.0f);       moveDetected=true; } break;
      case 2: if (y<THRESHOLD_DOWN)  { amplitude=min((float)(THRESHOLD_DOWN-y)/(float)(THRESHOLD_DOWN-SATURATION_LOW),1.0f);     moveDetected=true; } break;
      case 3: if (x>THRESHOLD_RIGHT) { amplitude=min((float)(x-THRESHOLD_RIGHT)/(float)(SATURATION_LIMIT-THRESHOLD_RIGHT),1.0f); moveDetected=true; } break;
      case 4: if (x<THRESHOLD_LEFT)  { amplitude=min((float)(THRESHOLD_LEFT-x)/(float)(THRESHOLD_LEFT-SATURATION_LOW),1.0f);     moveDetected=true; } break;
    }
  } else {
    bool xDom=absDx>absDy, yDom=absDy>absDx;
    if (absDx>500&&absDy>500) { xDom=absDx>(absDy*1.5); yDom=absDy>(absDx*1.5); }
    if (xDom) {
      if (x<THRESHOLD_LEFT&&absDx>DEADZONE_NEGATIVE)   { direction=4; amplitude=min((float)(THRESHOLD_LEFT-x)/(float)(THRESHOLD_LEFT-SATURATION_LOW),1.0f);     moveDetected=true; lockedDirection=4; directionLockTime=now; }
      else if (x>THRESHOLD_RIGHT&&absDx>DEADZONE_POSITIVE) { direction=3; amplitude=min((float)(x-THRESHOLD_RIGHT)/(float)(SATURATION_LIMIT-THRESHOLD_RIGHT),1.0f); moveDetected=true; lockedDirection=3; directionLockTime=now; }
    }
    if (!moveDetected&&yDom) {
      if (y>THRESHOLD_UP&&absDy>DEADZONE_POSITIVE)    { direction=1; amplitude=min((float)(y-THRESHOLD_UP)/(float)(SATURATION_LIMIT-THRESHOLD_UP),1.0f);       moveDetected=true; lockedDirection=1; directionLockTime=now; }
      else if (y<THRESHOLD_DOWN&&absDy>DEADZONE_NEGATIVE) { direction=2; amplitude=min((float)(THRESHOLD_DOWN-y)/(float)(THRESHOLD_DOWN-SATURATION_LOW),1.0f);     moveDetected=true; lockedDirection=2; directionLockTime=now; }
    }
  }

  if (moveDetected&&amplitude>0) {
    int delayTime=JOYSTICK_MAX_DELAY-(int)(amplitude*(JOYSTICK_MAX_DELAY-JOYSTICK_MIN_DELAY));
    if (now-lastJoystickMove>=(unsigned long)delayTime) {
      directionLockTime=now;
      uint8_t cmdId=0x00;
      switch (direction) { case 1:cmdId=currentAssignments[8];break; case 2:cmdId=currentAssignments[9];break; case 3:cmdId=currentAssignments[10];break; case 4:cmdId=currentAssignments[11];break; }
      executeCommandById(cmdId);
      lastJoystickState=direction; lastJoystickMove=now;
    }
  }
}

// ==================== CAMBIO MAPPA ====================

void checkKeymapChange() {
  if (otaBleActive) return;
  unsigned long now=millis();
  bool k1=TRAX.isPressed('1'), k2=TRAX.isPressed('2');
  if (!firstClickActive) {
    if (k1&&!key1Detected&&!key2Detected) { key1Detected=true; detectionWindow=now; }
    if (k2&&!key2Detected&&!key1Detected) { key2Detected=true; detectionWindow=now; }
    if ((key1Detected&&k2&&(now-detectionWindow<maxDetectionWindow))||
        (key2Detected&&k1&&(now-detectionWindow<maxDetectionWindow))||(k1&&k2)) firstClickActive=true;
    if ((key1Detected||key2Detected)&&(now-detectionWindow>maxDetectionWindow)) { key1Detected=false; key2Detected=false; }
  }
  if (firstClickActive&&!k1&&!k2) {
    if (!firstClickCompleted) { firstClickCompleted=true; firstClickTime=now; }
    firstClickActive=false; key1Detected=false; key2Detected=false;
  }
  if (firstClickCompleted) {
    if (firstClickActive&&(now-firstClickTime<doubleClickWindow)) {
      if (now-lastKeymapChange>keymapChangeDelay) performKeymapChange(now);
      firstClickCompleted=false; firstClickActive=false; key1Detected=false; key2Detected=false;
    }
    if (now-firstClickTime>doubleClickWindow) firstClickCompleted=false;
  }
}

void performKeymapChange(unsigned long now) {
  current_keymap=(current_keymap<3)?current_keymap+1:1;
  lastKeymapChange=now;
  loadAssignmentsFromEEPROM(current_keymap);
  loadRepeatFlagsFromEEPROM(current_keymap);
  Serial.printf("Mappa: %d\n", current_keymap);
  for (int i=0;i<3;i++) { setColor(255,255,255); delay(80); setColor(0,0,0); delay(80); }
  // Mostra il colore assegnato alla nuova mappa
  int m=current_keymap-1;
  setColor(mapColor[m][0], mapColor[m][1], mapColor[m][2]);
  delay(1000);
}

// ==================== LED ====================

void colore(unsigned char r, unsigned char g, unsigned char b) {
  static unsigned long ultimoCambiamento=0;
  static bool acceso=false;
  unsigned long ora=millis();
  unsigned long tempoAcceso=intervalloLampeggio*dutyCycle;
  unsigned long tempoSpento=intervalloLampeggio-tempoAcceso;
  if (acceso  && ora-ultimoCambiamento>=tempoAcceso) { setColor(0,0,0); acceso=false; ultimoCambiamento=ora; }
  if (!acceso && ora-ultimoCambiamento>=tempoSpento) { setColor(r,g,b); acceso=true;  ultimoCambiamento=ora; }
}

// Xiao ESP32-S3: LED common cathode, nessuna inversione (differisce da v02)
// Core 2.x: ledcWrite(channel, value) — non ledcWrite(pin, value)
void setColor(uint8_t r, uint8_t g, uint8_t b) {
  ledcWrite(LEDC_CH_RED,   r);
  ledcWrite(LEDC_CH_GREEN, g);
  ledcWrite(LEDC_CH_BLUE,  b);
}

// ==================== SETUP ====================

void setup() {
  Serial.begin(115200);
  Serial.println("=== HUGEROCK DISCOVERY PAD v03 ===");

  pinMode(LED_RED,   OUTPUT); pinMode(LED_GREEN, OUTPUT); pinMode(LED_BLUE, OUTPUT);
  // LEDC core 2.x: canale esplicito → ledcSetup + ledcAttachPin
  ledcSetup(LEDC_CH_RED,   5000, 8);
  ledcSetup(LEDC_CH_GREEN, 5000, 8);
  ledcSetup(LEDC_CH_BLUE,  5000, 8);
  ledcAttachPin(LED_RED,   LEDC_CH_RED);
  ledcAttachPin(LED_GREEN, LEDC_CH_GREEN);
  ledcAttachPin(LED_BLUE,  LEDC_CH_BLUE);
  setColor(255,255,255);

  pinMode(Vpin, INPUT_PULLUP);
  pinMode(EXT_BTN_PIN, INPUT_PULLUP);

  // USB prima di BLE: richiesto su Xiao ESP32-S3 (identico a v02)
  USB.begin();
  usbKeyboard.begin();
  delay(100);

  EEPROM.begin(512);
  delay(100);

  byte saved=EEPROM.read(EEPROM_KEYMAP_ADDR);
  current_keymap=(saved>=1&&saved<=3)?saved:1;
  if (saved<1||saved>3) { EEPROM.write(EEPROM_KEYMAP_ADDR,1); EEPROM.commit(); }

  loadAssignmentsFromEEPROM(current_keymap);
  loadRepeatFlagsFromEEPROM(current_keymap);
  loadWheelFlagsFromEEPROM();

  setupBLE();

  // Preferences dopo BLE: NVS già inizializzato da NimBLE
  loadMapColors();

  TRAX.addEventListener(keypad_handler);
  TRAX.setHoldTime(long_press_time);

  Serial.println("READY");
  for (int i=0;i<2;i++) { setColor(255,255,255); delay(150); setColor(0,0,0); delay(150); }
}

// ==================== LOOP ====================

void loop() {
  if (otaBleActive) { setColor(255,255,255); delay(10); return; }

  if (current_keymap!=key_mem) {
    EEPROM.write(EEPROM_KEYMAP_ADDR,current_keymap);
    EEPROM.commit();
    key_mem=current_keymap;
  }

  if (!firstConn&&bleConnected) {
    setColor(255,0,0); delay(500);
    setOptimalConnectionParams();
    firstConn=true;
  }

  // Preview colore dopo CONFIG:MAP_COLOR (1 secondo fisso, poi torna al lampeggio)
  if (showColorPreview) {
    if (millis()-colorPreviewTime>=1000) showColorPreview=false;
    else setColor(previewColor[0],previewColor[1],previewColor[2]);
  }

  if (!showColorPreview) {
    if (configModeActive) {
      setColor(255,0,0);
    } else {
      // LED fisso al colore della mappa attiva (v03: solid, non lampeggio come v02)
      int m=current_keymap-1;
      setColor(mapColor[m][0], mapColor[m][1], mapColor[m][2]);
    }
  }

  int v=digitalRead(Vpin);
  if (v==LOW&&Mvel==0)   { Mvel=1; if (wheelEnabled[current_keymap-1]) executeCommandById(currentAssignments[13]); }
  else if (v==HIGH&&Mvel==1) Mvel=0;

  checkKeymapChange();
  handle_joystick();
  TRAX.getKey();

  unsigned long now=millis();
  int extState=digitalRead(EXT_BTN_PIN);
  if (extState!=extBtnLast&&(now-extBtnLastTime)>debounceDelay) {
    extBtnLastTime=now; extBtnLast=extState;
    if (extState==LOW) executeCommandById(currentAssignments[12]);
  }

  delay(5);
}
