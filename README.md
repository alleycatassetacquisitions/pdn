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
- **Espressif32 Platform**: The firmware builds on PlatformIO's [espressif32](https://github.com/platformio/platform-espressif32) platform with `framework = espidf`. Installing it brings the whole device toolchain with it, so no separate CMake, Ninja, xtensa or esptool install is needed.
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

### IDE extensions

Builds always use **PlatformIO Core** and PlatformIO's official [espressif32](https://github.com/platformio/platform-espressif32) platform (see `platformio.ini`). You can build and flash from the terminal without any extension.

For in-editor integration, either extension (PlatformIO/pioarduino) is fine; pick based on your editor:

- **Microsoft VS Code**: [PlatformIO IDE](https://platformio.org/install/ide?install=vscode).
- **Other VS Code–compatible editors** (Cursor, VSCodium, etc.): PlatformIO IDE is unavailable there because it depends on Microsoft's C/C++ IntelliSense. Use the [pioarduino IDE](https://github.com/pioarduino/pioarduino-vscode-ide) extension instead—it still drives PlatformIO Core against this repo's `platformio.ini`.

### Cleaning up after the pioarduino platform

If you previously built with the pioarduino *platform* package, it and the official platform install into the same directories. Remove the old packages once:

```bash
# Remove old platform packages
rm -rf ~/.platformio/platforms/espressif32*
rm -rf ~/.platformio/packages/framework-arduinoespressif32*
rm -rf ~/.platformio/packages/tool-esptoolpy*

# Clean the project build cache
rm -rf .pio/

# Rebuild
pio run -e esp32-s3_pdn_release
```

## Usage

1. **Power on the PDN**: Once powered, the PDN will ask for your player registration code. This is a 4 digit code that is transmitted to the server to fetch player details. The **top button** cycles through digits, 0 through 9, while the **bottom button** functions as a confirm operation.

2. **Log in to Device**: After entering the code, the device will attempt to fetch data from the server. If there is no server running, it will default to offline mode. You will need to **confirm the player code**, **select a role - Hunter/Bounty** and then confirm. The device will launch into the idle state and you will then be able to practice quickdraw duels with another device.

3. **For Simulated Development**: See CLI_README.md for development without a hardware device.

## Development

### Local Development

If you want to contribute to the PDN Project, follow these steps to set up your local development environment:

1. **Fork the repository** and create your development branch:
   ```bash
   git checkout -b feature/NewFeature
   ```

2. **Develop your feature**
3. **Test on Device/CLI** - CLI tests are sufficient if the hardware layer is unchanged, if any changes are made to drivers our core device logic, features need to be tested on device as well.
3. **Add any new tests** - these should be located in `test/test_core` or `test/test_cli`
4. **Run the test suite** - ensure your changes haven't broken any unit tests.
   ```bash
   pio test -e native          # Core unit tests
   pio test -e native_cli_test # CLI-specific tests
   ```
   CI also gates on the core suite under sanitizers. Run those binaries directly:
   a default `pio test` prints only lines its googletest parser recognizes, so a
   sanitizer report is dropped.
   ```bash
   pio test -e native_asan --without-testing && ./.pio/build/native_asan/program
   pio test -e native_tsan --without-testing && ./.pio/build/native_tsan/program
   ```
5. **Commit and push your changes**:
   ```bash
   git commit -m "Add New Feature"
   git push origin feature/NewFeature
   ```
6. **Create a pull request** for review. Ensure your PR is passing all checks before merging.

### Code Structure

#### `libraries`
- `core/`: Fundamental architecture for Alleycat embedded devices. This library is platform agnostic.
- `esp32-drivers/`: drivers for the esp32-s3 family of chips. Relies on `core/device/drivers/`
- `native-drivers/`: drivers that simulate the hardware of the pdn for our testing and simulator platform.
- `alleycat-server/`: api's for the alleycat-rs backend.

#### `src`
- `cli`: `core/` + `native-drivers` TUI app for simulating up to 16 pdn's.
- `fdn`: `core/` + `esp32-drivers` Game & Application Logic for the Alleycat Fixed Data Node.
- `pdn`: `core/` + `esp32-drivers` Game & Application Logic for the Alleycat Portable Data Node.

#### other
- `test/`: Unit testing files for ensuring code stability.
- `platformio.ini`: Project configuration file for PlatformIO, defining build environments and dependencies.

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