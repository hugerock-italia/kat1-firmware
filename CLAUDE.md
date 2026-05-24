# KAT-ADV Companion App — Project Context

Questo file viene caricato automaticamente da Claude Code in ogni sessione. Contiene il contesto persistente del progetto: cos'è, com'è organizzato, e le convenzioni di lavoro.

## Cos'è il progetto

App Flutter companion per la pulsantiera **KAT-ADV** (KAT1), un dispositivo per navigazione in motorally basato su microcontrollore ESP32-S3.

Funzioni principali dell'app:
- Configurazione della pulsantiera via BLE (Bluetooth Low Energy)
- Aggiornamento firmware via OTA (BLE)
- Visualizzazione stato di connessione
- Gestione mappe di tasti per app diverse (DMD2, Whip Live, ecc.)

Repository GitHub: `hugerock-italia/HUGEROCK_PAD_CONFIGURATOR`

## Stack tecnico

- **Flutter** 3.x con null-safety (versione esatta in `pubspec.yaml`)
- **BLE**: `flutter_blue_plus`
- **Permessi runtime**: `permission_handler`
- **Versione app**: `package_info_plus`
- **State management**: usa il pattern già adottato nei file esistenti, non introdurre librerie nuove senza richiesta esplicita

## Struttura cartelle (progetto)

```
HUGEROCK_PAD_CONFIGURATOR/
├── lib/
│   ├── main.dart                # entry point, definisce AppColors, AppTheme
│   ├── screens/                 # schermate principali dell'app
│   │   ├── home_screen.dart
│   │   ├── config_screen.dart
│   │   ├── ota_screen.dart
│   │   └── auto_config_screen.dart
│   └── services/                # logica di business
│       └── ble_manager.dart
├── android/                     # configurazione Android nativa
├── assets/                      # immagini, font, icone
├── test/                        # test unitari e widget
├── pubspec.yaml                 # dipendenze e metadata
├── .claude/
│   ├── agents/                  # sub-agenti Claude Code
│   └── settings.local.json      # configurazione locale (gitignored, contiene segreti)
└── CLAUDE.md                    # questo file
```

## Convenzioni di codice

- Codice in **inglese**: identificatori, stringhe utente, dartdoc. Commenti in italiano accettabili dove aiutano chiarezza
- Naming Dart standard: `lowerCamelCase` per variabili e metodi, `UpperCamelCase` per classi, `snake_case.dart` per file
- Doc inline: dartdoc per ogni API pubblica nuova (`///` sopra la dichiarazione)
- Niente `print()` in produzione: usare `debugPrint`

## Convenzioni Git e GitHub

- **Commit messages**: Conventional Commits in inglese. Esempi:
  - `feat(app): add update check on startup`
  - `fix(app): handle BLE disconnect gracefully`
  - `docs(app): update README with build instructions`
  - `chore: bump flutter_blue_plus to 1.32.0`
- **Branch naming**: `feat/<slug>`, `fix/<slug>`, `chore/<slug>`, `docs/<slug>`
- **Pull request**: sempre da feature branch verso `main`. **MAI** push diretto su `main`
- **PR body**: motivazione + change list + screenshot per modifiche UI

## Rete di agenti Claude Code

Il progetto usa una rete di sub-agenti in `.claude/agents/`. Fase attuale del piano: **C** (5 agenti operativi).

Agenti attivi:
- **orchestrator** — decompone i task, smista, valida UX gate
- **app-engineer-flutter** — modifica codice Dart e configurazioni
- **github-operator** — operazioni Git e GitHub (branch, commit, push, PR)
- **fw-engineer-arduino** — modifica sketch Arduino (.ino) per ESP32-S3; applica regola common/board-specific
- **fw-build-validator** — compila con arduino-cli, valida dimensione binario rispetto allo slot OTA

Documento di architettura completo della rete: `KAT-ADV-Agent-Network.md` nel root del repo (se presente).

## REGOLA D'ORO per usare gli agenti

**Una sola richiesta all'orchestrator per task.** Includi tutto il task (modifica + commit + push + PR) in una singola frase iniziale. **Non spezzettare turno per turno** — se inizi a dire "ok adesso committa", "ok adesso pusha", Claude Code principale prende il controllo e bypassa gli agenti specializzati.

Esempio CORRETTO:
> `@orchestrator aggiungi la sezione X al README, committa "docs: ..." su feature branch e apri la PR`

Esempio SBAGLIATO:
> Turno 1: `@orchestrator aggiungi sezione X`
> Turno 2: `commit this`
> Turno 3: `push it`

## Regole di base per gli agenti

- **Read before write**: prima di modificare un file, leggerlo sempre
- **Edit, non Write**: per modificare file esistenti usare il tool `Edit` con modifiche puntuali. `Write` è solo per file nuovi
- **No workaround creativi**: niente Python/Bash/heredoc per scrivere file Dart. Se `Edit` fallisce, segnalare l'errore, non improvvisare
- **UX changes richiedono conferma utente**: nuovi dialog, modifiche visibili → l'agente deve chiedere PRIMA di procedere
- **Validazione locale prima del commit**: `dart format` e `flutter analyze` devono uscire puliti

## Note ambientali

- Sistema: **Windows**
- Auth GitHub: gestita da GitHub Desktop (Git Credential Manager). gh CLI installato ma autenticazione separata in `.claude/settings.local.json` (file gitignored)
- Path semplice (`C:\HUGEROCK\APP\HUGEROCK_PAD_CONFIGURATOR\`) per evitare problemi bash con caratteri speciali

## Track firmware (Fase C)

### Repository
`https://github.com/hugerock-italia/kat1-firmware` (privato)
Path locale: `C:\HUGEROCK\FW\kat1-firmware`

### Struttura attesa
```
kat1-firmware/
├── DISCOVERY_03_XIAO/
│   └── DISCOVERY_03_XIAO.ino   # dev board Xiao ESP32-S3, joystick analogico Hall
└── EXTREME_05/
    └── EXTREME_05.ino           # levetta digitale
```
I due sketch sono ~90% identici. Strategia: ZERO refactor verso libreria condivisa. Ogni modifica va classificata "common" o "board-specific" — vedi regola nel profilo fw-engineer-arduino.

### Toolchain — Setup Windows
```
winget install ArduinoSA.CLI
arduino-cli core install esp32:esp32@2.0.17
arduino-cli lib install "NimBLE-Arduino@1.4.2" "Keypad"
```
Prerequisito: arduino-cli deve essere nel PATH prima di usare fw-build-validator.

### FQBN board
- **Dev (Xiao ESP32-S3):** `esp32:esp32:XIAO_ESP32S3`
- **Prod (ESP32-S3-WROOM-1U):** `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,MSCOnBoot=default,DFUOnBoot=default,UploadMode=default,CPUFreq=240,FlashMode=qio,FlashSize=4M,PartitionScheme=default,DebugLevel=none,PSRAM=disabled,LoopCore=1,EventsCore=1,EraseFlash=none,JTAGAdapter=default`

### Slot OTA produzione
Partition: "Default 4MB with spiffs (1.2MB APP/1.5MB SPIFFS)"
Slot app max: **1.228.800 bytes**
fw-build-validator usa questa soglia: > 95% = warning, > 100% = fail bloccante.

### Protocollo OTA BLE (riepilogo)
1. App Flutter invia `CONFIG:OTA:BEGIN:<size>` sulla characteristica di controllo
2. App invia il binario a chunk su `otaCharacteristic`
3. App invia `CONFIG:OTA:END`
4. Il device verifica il binario e riavvia sul nuovo firmware

### Regola common vs board-specific
- **Common**: modifica identica su entrambi gli sketch → fw-engineer-arduino applica su DISCOVERY e EXTREME nello stesso turno
- **Board-specific**: modifica su un solo sketch → fw-engineer-arduino dichiara quale e perché l'altro non è coinvolto

### Agenti del track firmware
- `fw-engineer-arduino` — scrive/modifica gli sketch .ino
- `fw-build-validator` — compila con arduino-cli e valida dimensione binario

### TODO — Verifica flash board produzione
Al primo build reale, verificare se la board è N8 (8MB flash) invece di N4 (4MB).
Se confermato N8: valutare passaggio a `FlashSize=8M` con partition `min_spiffs` per raddoppiare lo spazio disponibile per l'app.

---

## ⚠️ Vincoli firmware — LEGGERE PRIMA DI MODIFICARE GLI SKETCH

Questa sezione documenta vincoli critici scoperti durante lo sviluppo. Ignorarli causa regressioni difficili da diagnosticare.

### Versioni toolchain — NON aggiornare senza verifica

| Componente | Versione validata | Note |
|---|---|---|
| Arduino ESP32 core | **2.0.17** | Core 3.x è incompatibile con NimBLE-Arduino v1.4.x — BLE smette di funzionare senza crash visibili |
| NimBLE-Arduino | **1.4.x** | v1.4 usa `esp_nimble_hci_and_controller_init()` rimossa in ESP-IDF 5.x (core 3.x) |

**NimBLE-Arduino v1.4.x + Arduino ESP32 core 3.x = BLE non avvia l'advertising senza errori visibili.** La diagnosi richiede ore. Non aggiornare il core senza aggiornare anche NimBLE a v2.x (ma v2.x ha API breaking changes).

### Hardware dev vs prod — differenze LED

| Board | LED tipo | `setColor()` |
|---|---|---|
| Dev — Xiao ESP32-S3 (`DISCOVERY_03_XIAO`) | Common **cathode** | `ledcWrite(ch, r/g/b)` diretto — nessuna inversione |
| Prod — ESP32-S3-WROOM (`DISCOVERY_03`) | Common **anode** | `ledcWrite(ch, 255-r/g/b)` — valori invertiti |

Non copiare `setColor()` da uno sketch all'altro senza adattare la polarità.

### API LEDC — core 2.x vs core 3.x

Il core 2.x usa un'API LEDC diversa dal core 3.x. Usare **sempre** la forma core 2.x:

```cpp
// ✅ CORRETTO — core 2.x
#define LEDC_CH_RED   0
#define LEDC_CH_GREEN 1
#define LEDC_CH_BLUE  2

// In setup():
ledcSetup(LEDC_CH_RED, 5000, 8);
ledcAttachPin(LED_RED, LEDC_CH_RED);

// In setColor():
ledcWrite(LEDC_CH_RED, r);   // primo argomento = CANALE, non pin
```

```cpp
// ❌ SBAGLIATO — core 3.x only, non compila su 2.x
ledcAttach(LED_RED, 5000, 8);
ledcWrite(LED_RED, r);        // LED_RED è un pin, non un canale
```

### Regole BLE — callback e GATT table

**Regola 1 — Mai `delay()` nei callback BLE.**
I callback `onWrite()` di NimBLE girano nel task BLE interno. Qualsiasi `delay()` blocca il task, causa timeout e crash del BLE stack. Per feedback visivi post-comando, usare un flag globale gestito nel `loop()`.

```cpp
// ❌ SBAGLIATO — dentro onWrite():
saveMapColor(mapN - 1, r, g, b);
setColor(r, g, b);
delay(1000);   // KILL: blocca il task BLE

// ✅ CORRETTO — dentro onWrite():
saveMapColor(mapN - 1, r, g, b);
previewColor[0] = r; previewColor[1] = g; previewColor[2] = b;
showColorPreview = true;
colorPreviewTime = millis();
// Il loop() gestisce il LED dopo
```

**Regola 2 — Limite GATT table ~60 attributi.**
NimBLEHIDDevice con 7 input report + Device Info + Battery Service + configService occupa già ~58-61 attributi. Aggiungere `NIMBLE_PROPERTY::NOTIFY` a `configCharacteristic` crea un CCCD extra e sfora il limite: il BLE non avvia l'advertising senza crash né errori.

```cpp
// ❌ SBAGLIATO — sfora il limite GATT
configCharacteristic = configService->createCharacteristic(
    uuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::NOTIFY);

// ✅ CORRETTO — usa READ per le risposte, l'app fa polling
configCharacteristic = configService->createCharacteristic(
    uuid, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::READ);
```

Per rispondere a query dell'app (es. `CONFIG:GET_COLORS`): `configCharacteristic->setValue(...)` senza `notify()`. L'app legge il valore con una READ esplicita.

**Regola 3 — Ordine inizializzazione in `setup()`.**
Su Xiao ESP32-S3, USB deve essere inizializzato **prima** di BLE. Se `NimBLEDevice::init()` viene chiamato prima di `USB.begin()`, crasha (panic) al boot.

```cpp
// ✅ ORDINE CORRETTO in setup():
USB.begin();
usbKeyboard.begin();
delay(100);
// ... EEPROM, load assignments ...
setupBLE();
loadMapColors();   // Preferences/NVS dopo BLE: NVS già inizializzato da NimBLE
```

**Regola 4 — `Preferences` (NVS) sempre dopo `setupBLE()`.**
`Preferences::begin()` chiama `nvs_flash_init()` internamente. Se chiamata prima di `NimBLEDevice::init()`, può causare conflitti di inizializzazione NVS. Tutte le chiamate a `loadMapColors()` / `saveMapColor()` vanno dopo `setupBLE()`.

### Protocollo comandi BLE firmware

| Comando app → device | Comportamento device |
|---|---|
| `CONFIG:MODE:ON` | Entra in config mode (LED rosso fisso) |
| `CONFIG:MODE:OFF` | Esce da config mode |
| `CONFIG:MAP_COLOR:N:R:G:B` | Imposta colore mappa N (1-3), salva in NVS |
| `CONFIG:GET_COLORS` | Scrive `R,G,B;R,G,B;R,G,B` su configCharacteristic → app legge con READ |
| `CONFIG:WHEEL:N:0/1` | Abilita/disabilita wheel encoder mappa N |
| `CONFIG:M:K:HH:R` | Assegna comando HH a tasto K mappa M, flag repeat R |
| `RESET:CONFIG` | Reset EEPROM + NVS colori → default |
| `CONFIG:OTA:BEGIN:<size>` | Avvia sessione OTA BLE |
| `CONFIG:OTA:END` | Finalizza OTA e riavvia |
