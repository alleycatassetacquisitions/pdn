# Portable Data Node (PDN) by Alleycat Asset Acquisitions

Welcome to the official repository for Alleycat Asset Acquisitions' **Portable Data Node (PDN)** project. The PDN is a cutting-edge, interactive device used in live-action role-playing (LARP) environments like [Neotropolis](https://www.neotropolis.com). It leverages real-time technology and augmented reality to create immersive gameplay experiences. The PDN serves as a versatile tool for communication, mission tracking, and player interaction.

[![PDN MK 2.1 Fusion Model](PDN%20MK%202.1.png)](https://a360.co/4ls4MnI)
Clicking on the image will take you to the fusion 360 project!

PCB's were designed in KiCad 8 and will be uploaded to a separate repository soon.

## Table of Contents

- [About the Project](#about-the-project)
- [Project Goals](#project-goals)
- [Features](#features)
- [Installation](#installation)
- [Usage](#usage)
- [The Game](#the-game)
- [Development](#development)
- [Contributing](#contributing)
- [License](#license)
- [Contact](#contact)

## About the Project

The Alleycat Asset Acquisitions Portable Data Node is the premiere device for facilitating immersive experiences and in person, interactive play. You can find Elli Furedy's talk from Hackaday Supercon 2025 on the PDN and Alleycat's philosophies [here](https://youtu.be/ndodsA254HA).

You can find the developer wiki [here](https://deepwiki.com/alleycatassetacquisitions/pdn).

## Project Goals

- **Immersive Icebreaker**: At it's core, the PDN is designed to facilitate face to face experiences.
- **Open Source Collaboration**: Foster collaboration by making the PDN's software and hardware designs available to the community for further improvement. Check out the PDN CLI tool for development tools without needing a physical device.

## Features

- **Extensible Platform**: The PDN currently supports a single experience - the Neotropolis Quickdraw game, however, it is trivial to create new State Machines to support new game experiences.
- **Peripheral Rich**: The base PDN design supports a 128x64 OLED, 19 LEDs, vibration haptics, 2 buttons, ESP-NOW, HTTP and Serial communication.
- **Real-Time Tracking**: The PDN supports connecting to a server to update player data in real time.
- **Modular Design**: The PDN framework works off of a "Driver" framework that allows for customizing the hardware, whether to support new hardware platforms or new peripherals.

## Installation

### Prerequisites

- **PlatformIO Core**: Ensure you have PlatformIO installed as it is used for development and flashing the firmware.
- **pioarduino Platform**: This project uses [pioarduino](https://github.com/pioarduino/platform-espressif32), a community fork of the Espressif32 platform with support for newer ESP-IDF and Arduino framework versions.
- **Unix Style Terminal**: Required for the CLI simulator tool. Windows users should use WSL (Windows Subsystem for Linux).

### Steps

1. Clone the repository:
   ```bash
   git clone https://github.com/AlleycatAssetAcquisitions/pdn
   ```

2. Navigate to the project directory:
   ```bash
   cd pdn/
   ```

3. Install PlatformIO Core:
   ```bash
   pip install platformio
   # or
   platformio upgrade
   ```

4. Configure WiFi credentials (for ESP32-S3 builds):
   ```bash
   cp wifi_credentials.ini.example wifi_credentials.ini
   ```
   Then edit `wifi_credentials.ini` with your WiFi SSID, password, and API base URL. This file is excluded from version control for security.

5. Build the project using PlatformIO:
   ```bash
   pio run -e <build-target>
   ```
   Depending on your use case, there are a number of build targets:
   - `esp32-s3_pdn_release` - Release build (NO LOGS)
   - `esp32-s3_pdn_debug` - Standard Development build
   - `native_cli` - Build the native CLI tool for simulated development (Unix/WSL only)

6. Flash the PDN firmware to your device:
   ```bash
   platformio run --target upload
   ```

### Migration from Standard PlatformIO Espressif32

This project recently migrated from the standard PlatformIO Espressif32 platform to **pioarduino** for access to newer ESP-IDF versions and improved Arduino framework support.

Uninstall PlatformIO from your IDE.
Install pioarduino.
Restart your IDE.

If you're experiencing build issues after pulling recent changes, perform a clean install:

```bash
# Remove old platform packages
rm -rf ~/.platformio/platforms/espressif32*
rm -rf ~/.platformio/packages/framework-arduinoespressif32*

# Clean the project build cache
rm -rf .pio/

# Rebuild
pio run -e esp32-s3_pdn_release
```

## Usage

1. **Power on the PDN**: Once powered, the PDN will ask for your player registration code. This is a 4 digit code that is transmitted to the server to fetch player details. The **top button** cycles through digits, 0 through 9, while the **bottom button** functions as a confirm operation.

2. **Log in to Device**: After entering the code, the device will attempt to fetch data from the server. If there is no server running, it will default to offline mode. You will need to **confirm the player code**, **select a role - Hunter/Bounty** and then confirm. The device will launch into the idle state and you will then be able to practice quickdraw duels with another device.

3. **For Simulated Development**: See [src/cli/CLI_README.md](src/cli/CLI_README.md) for development without a hardware device.

## The Game

The PDN is a social icebreaker first. Every mechanic below requires physically cabling your device to someone else's, and the games are designed to pull strangers at an event into face-to-face play. The cables aren't a data link that happens to be social; they're a social mechanic that happens to carry data.

At registration, every player picks a role: **Hunter** or **Bounty**.

- **Quickdraw Duel**: Cable your PDN to a player of the opposite role. Both devices count down together, the screens flash DRAW, and the fastest button press wins. Reaction times, wins, and losses are tracked on the device and uploaded when a server is reachable.

- **Chain Duel**: Players who share your role can daisy-chain their devices behind yours. The device at the head of each same-role run (the one facing an enemy device) is that team's **champion**; everyone behind is a **supporter**. Supporters lock in before the duel, and each locked-in supporter shaves 15ms off their champion's reaction time. Supporters watch the countdown, the draw, and the result live on their own screens.

- **Shootout**: Close a chain into a ring (plug the last device into the first) and the ring proposes a tournament: a shuffled bracket where players duel one-on-one, losers spectate, and matches continue until one player is left standing. Up to 16 devices can play a single shootout.

## Development

### Local Development

The fastest way to see the system working is the CLI simulator: a network of simulated PDNs in your terminal, no hardware required:

```bash
pio run -e native_cli
.pio/build/native_cli/program 2
```

Type `cable 0 1` to connect the two devices over a virtual serial cable and watch them discover each other, then drive the selected device's buttons (`↑`/`↓`) through a quickdraw duel. [src/cli/CLI_README.md](src/cli/CLI_README.md) documents all simulator commands.

To contribute a change:

1. **Fork the repository** and create your development branch:
   ```bash
   git checkout -b feature/NewFeature
   ```

2. **Develop your feature**
3. **Test on Device/CLI** - CLI tests are sufficient if the hardware layer is unchanged; if any changes are made to drivers or core device logic, features need to be tested on device as well.
4. **Add any new tests** - these should be located in `test/test_core` or `test/test_cli`
5. **Run the test suite** - ensure your changes haven't broken any unit tests.
   ```bash
   pio test -e native          # Core unit tests
   pio test -e native_cli_test # CLI-specific tests
   ```
   `pio test` rebuilds and re-runs everything; when iterating, running the built test binary directly is much faster:
   ```bash
   .pio/build/native/program
   ```
6. **Commit and push your changes**:
   ```bash
   git commit -m "Add New Feature"
   git push origin feature/NewFeature
   ```
7. **Create a pull request** for review. Ensure your PR is passing all checks before merging.

### Code Structure

- `lib/core/`: The platform-independent device layer. `include/device/` holds the hardware driver interfaces plus the serial connectivity layer (`RemoteDeviceCoordinator`, peer-graph); `include/wireless/` is the reliable ESP-NOW transport (`WirelessTransport` / `ReliableChannel`); `include/state/` is the state-machine framework that powers every game; `include/game/` has the shared `Player` / `Match` records.
- `lib/esp32-drivers/`: Concrete ESP32-S3 implementations of the driver interfaces (display, LEDs, buttons, haptics, ESP-NOW radio, HTTP).
- `lib/native-drivers/`: The same driver interfaces implemented for desktop; these power the CLI simulator and the native test suite.
- `src/pdn/`: The PDN firmware itself. `main.cpp` is the entry point and platform loop; `game/` contains Quickdraw and its sub-modes (chain duels, shootout tournaments); `apps/` holds the player-registration flow; `device/` assembles the concrete device.
- `src/cli/`: The terminal simulator ([src/cli/CLI_README.md](src/cli/CLI_README.md)).
- `test/test_core` and `test/test_cli`: Unit and integration tests, run on the native platform.
- `platformio.ini`: Build environments and dependencies; `boards/` has the ESP32-S3 board definition; `scripts/` holds flashing and build helpers.

New to the code? Start at `src/pdn/main.cpp`, then `Quickdraw::populateStateMap()` in `src/pdn/game/quickdraw.cpp` for the game-state graph. The header comments in `lib/core/include/wireless/wireless-transport.hpp` (wireless packet lifecycle) and `lib/core/include/device/remote-device-coordinator.hpp` (serial connectivity) are the best protocol documentation in the repo; see also the Architecture in Practice section of [CONTRIBUTING.md](CONTRIBUTING.md).

## Contributing

Alleycat wants YOUR HELP! Whether you have feature suggestions, bug reports, or improvements, please follow our [contributing guidelines](CONTRIBUTING.md).

### How to Contribute

1. **Fork the repository**.
2. **Create a new branch** for your feature or bug fix.
3. **Commit your changes** with a descriptive message.
4. **Submit a pull request** for review.

## License

This project is licensed under the MIT License. See the [LICENSE](LICENSE) file for details.

## Contact

For any questions, collaboration opportunities, or technical issues, please reach out to us:

- **Email**: [alleycatassetacquisitions@gmail.com](mailto:alleycatassetacquisitions@gmail.com)
- **Website**: [alleycat.agency](https://alleycat.agency)