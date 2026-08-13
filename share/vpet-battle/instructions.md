# VPet Battle Service — Integration Guide

This folder contains a reusable ESP32/Arduino BLE battle service. It deliberately contains no display, touch, keyboard, sound, sprite, or pet-storage code. Each device can provide its own interface while using the same battle rules and wire protocol.

## Requirements

- An ESP32 supported by the Arduino framework
- PlatformIO or Arduino IDE
- [NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) 2.5.1
- Arduino `WiFi` support
- C++17 or the standard used by the ESP32 Arduino core

For PlatformIO:

```ini
lib_deps =
    h2zero/NimBLE-Arduino@2.5.1
```

## Files to copy

Copy both files into your project:

```text
include/familiar_battle_service.h
src/familiar_battle_service.cpp
```

Then include the header and create one service instance:

```cpp
#include "familiar_battle_service.h"

FamiliarBattleService battle;
```

## Pet identity

Every participant supplies:

- `playerId`: a stable 32-bit identifier for the player or pet
- `stageIndex`: an agreed numeric evolution-stage index
- `level`: the pet's current level

The current protocol transmits these values during discovery and the HELLO handshake. Raw combat stats are never accepted from the peer; both devices derive them locally from level and stage.

A simple ESP32 hardware-derived ID is:

```cpp
uint64_t mac = ESP.getEfuseMac();
uint32_t playerId = static_cast<uint32_t>(mac ^ (mac >> 32));
```

For a transferable pet identity, store a generated ID in NVS instead of tying it to the physical board.

## Stat formulas

The service is the authority for combat stats:

```cpp
maxHp  = 20 + level * 3;
attack = min(255, 4 + level + stageIndex * 2);
defense = min(255, 2 + level / 2 + stageIndex);
```

Use the public helpers when displaying stats:

```cpp
uint16_t hp = FamiliarBattleService::deriveMaxHp(level);
uint8_t attack = FamiliarBattleService::deriveAttack(level, stageIndex);
uint8_t defense = FamiliarBattleService::deriveDefense(level, stageIndex);
```

Do not duplicate or modify these formulas on only one device. Both peers must resolve turns identically.

## Main loop

Call `update()` frequently whenever hosting, connecting, or battling. NimBLE callbacks only queue inbound bytes; `update()` performs the actual battle-state changes on the application's task.

```cpp
void loop() {
    battle.update();

    switch (battle.state()) {
        case FamiliarBattleState::Battling:
            // Draw only when relevant values change.
            break;
        case FamiliarBattleState::Result:
            // Show battle.outcome().
            break;
        default:
            break;
    }
}
```

Avoid long blocking UI operations during an active connection.

## Hosting a battle

The host advertises the VPet service and waits for a challenger:

```cpp
bool started = battle.beginHost(playerId, stageIndex, level);
```

Expected state flow:

```text
Idle -> Hosting -> Battling -> Result
```

Display `battle.status()` while waiting. Call `battle.end()` when the user cancels or leaves the battle interface.

## Finding and challenging an opponent

Scanning is bounded and blocking for approximately four seconds:

```cpp
battle.beginFind(playerId, stageIndex, level);

const auto& opponents = battle.scanResults();
for (size_t i = 0; i < opponents.size(); ++i) {
    // Present opponents[i].level, playerId and rssi in your own UI.
}
```

Connect using the chosen result index:

```cpp
if (!opponents.empty()) {
    battle.connectTo(0);
}
```

Expected state flow:

```text
Idle -> Scanning -> Idle -> Connecting -> Battling -> Result
```

A result with `playerId == 0` is still valid. It means the peer advertised the service UUID without the optional manufacturer identity payload. Its real identity and stats arrive during the HELLO handshake.

## Moves

The four protocol moves are:

```cpp
FamiliarBattleMove::Attack;
FamiliarBattleMove::Defend;
FamiliarBattleMove::Special;
FamiliarBattleMove::Flee;
```

Submit one move per turn:

```cpp
if (battle.state() == FamiliarBattleState::Battling &&
    !battle.myMoveSubmitted()) {
    battle.submitMove(FamiliarBattleMove::Attack);
}
```

Disable move controls while `myMoveSubmitted()` is true. The service waits until both players have submitted, then resolves the turn deterministically on both devices.

Attack deals normal damage. Defend halves incoming damage for that turn. Special deals 1.5 times calculated damage but has a 15% miss chance. Flee immediately ends the local battle and informs the peer.

Damage has deterministic ±20% variance. Both devices use the challenger's shared PRNG seed and resolve the lower player ID first, ensuring they consume random values in the same order.

## Displaying an active battle

Useful getters are:

```cpp
battle.myHp();
battle.myMaxHp();
battle.opponentHp();
battle.opponentMaxHp();
battle.turnNumber();
battle.opponent().playerId;
battle.opponent().stageIndex;
battle.opponent().level;
battle.log();
```

`battle.log()` contains recent human-readable events. It retains at most 40 entries. Rendering is entirely the integrating application's responsibility.

## Outcomes

When the state becomes `FamiliarBattleState::Result`, inspect:

```cpp
switch (battle.outcome()) {
    case FamiliarBattleOutcome::Victory: break;
    case FamiliarBattleOutcome::Defeat: break;
    case FamiliarBattleOutcome::Fled: break;
    case FamiliarBattleOutcome::OpponentFled: break;
    case FamiliarBattleOutcome::Disconnected: break;
    default: break;
}
```

Call `battle.end()` before returning to normal application operation. Leaving an active battle through `end()` appears as a disconnection to the opponent; use `submitMove(FamiliarBattleMove::Flee)` when a deliberate flee should be recorded.

## BLE protocol version 1

The implementation uses the Nordic UART Service UUID convention:

```text
Service:          6E400001-B5A3-F393-E0A9-E50E24DCCA9E
Write:            6E400002-B5A3-F393-E0A9-E50E24DCCA9E
Notify:           6E400003-B5A3-F393-E0A9-E50E24DCCA9E
Protocol version: 1
```

The challenger writes to the host. The host sends notifications to the challenger. Multi-byte integers use network byte order (big-endian).

### HELLO — message `0x01`

Exactly 13 bytes:

```text
Offset  Size  Value
0       1     Message type: 0x01
1       4     Player ID, uint32 big-endian
5       1     Stage index
6       1     Level
7       2     Current HP, uint16 big-endian
9       4     Shared PRNG seed, uint32 big-endian
```

Only the challenger creates the shared seed. The host adopts it and returns a HELLO response. A received HP value of zero is interpreted as derived maximum HP during the handshake.

### MOVE — message `0x02`

Exactly 2 bytes:

```text
Offset  Size  Value
0       1     Message type: 0x02
1       1     Move: Attack=0, Defend=1, Special=2, Flee=3
```

### Optional discovery payload

Manufacturer data uses company ID `0xFFFF`, reserved for testing:

```text
FF FF 56 50 01 [playerId:4] [stage:1] [level:1] [enabled:1]
```

Peers must also accept devices advertising only the service UUID. This is required for compatibility with desktop BLE peripheral libraries that cannot reliably set custom manufacturer data.

## Radio ownership

ESP32 Wi-Fi and BLE share radio resources. The service disables Wi-Fi before starting NimBLE. Do not attempt Wi-Fi synchronisation while a battle is active. After `battle.end()`, another subsystem may re-enable Wi-Fi if required.

## Python simulator

This folder includes the compatible `vpet_battle_simulator.py`. It requires Linux Bluetooth peripheral support and the `bless` package:

```bash
python3 -m pip install bless
python3 vpet_battle_simulator.py --level 10 --stage 2
```

The simulator acts as a host. On the device, use Find Opponent and connect to `Ghostwire VPet Sim`.

Interactive simulator battles are available with:

```bash
python3 vpet_battle_simulator.py --level 10 --stage 2 --interactive
```

## Compatibility rules

For devices to remain interoperable:

1. Do not change the UUIDs or packet byte order.
2. Do not change move numeric values.
3. Do not change stat or damage formulas for only one implementation.
4. Preserve the xorshift32 PRNG exactly.
5. Preserve lower-player-ID-first resolution order.
6. Keep protocol changes behind a new explicit protocol version.
7. Derive peer stats from level and stage instead of trusting arbitrary transmitted stats.
8. Continue accepting service-UUID-only advertisements.

UI design, sprites, animation, sounds, controls, transport-status presentation, pet progression, rewards, and persistent records may differ freely without affecting protocol compatibility.

## Minimal lifecycle

```cpp
#include <Arduino.h>
#include "familiar_battle_service.h"

FamiliarBattleService battle;
constexpr uint8_t level = 5;
constexpr uint8_t stage = 2;

uint32_t playerId() {
    const uint64_t mac = ESP.getEfuseMac();
    return static_cast<uint32_t>(mac ^ (mac >> 32));
}

void setup() {
    Serial.begin(115200);
    battle.beginHost(playerId(), stage, level);
}

void loop() {
    battle.update();

    if (battle.state() == FamiliarBattleState::Battling &&
        !battle.myMoveSubmitted()) {
        // Replace this delay/automatic move with your own input handling.
        delay(1000);
        battle.submitMove(FamiliarBattleMove::Attack);
    }

    if (battle.state() == FamiliarBattleState::Result) {
        Serial.printf("Battle ended with outcome %u\n",
                      static_cast<unsigned>(battle.outcome()));
        battle.end();
    }
}
```

This example automatically hosts and attacks; production applications should expose cancellation, move selection, connection status, and result handling through their own interface.
