# kat1-firmware

Firmware Arduino per la pulsantiera KAT-ADV (KAT1) basata su ESP32-S3.

## Struttura cartelle

```
kat1-firmware/
├── DISCOVERY/
│   └── DISCOVERY_03.ino     # Variant with analog Hall joystick
└── EXTREME/
    └── EXTREME_05.ino       # Variant with digital lever switch
```

## Build instructions (arduino-cli)

### Dev — Xiao ESP32-S3

```bash
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 DISCOVERY/DISCOVERY_03.ino
```

### Prod — ESP32-S3-WROOM-1U

```bash
arduino-cli compile --fqbn "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio80,FlashSize=4M,PartitionScheme=default,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default" DISCOVERY/DISCOVERY_03.ino
```

Sostituire `DISCOVERY/DISCOVERY_03.ino` con `EXTREME/EXTREME_05.ino` per compilare la variante EXTREME.
