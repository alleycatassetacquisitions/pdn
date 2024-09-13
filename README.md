# Portable Data Node (PDN) by Alleycat Asset Acquisitions

Welcome to the official repository for Alleycat Asset Acquisitions' **Portable Data Node (PDN)** project. The PDN is a cutting-edge, interactive device used in live-action role-playing (LARP) environments like [Neotropolis](https://www.neotropolis.com). It leverages real-time technology and augmented reality to create immersive gameplay experiences. The PDN serves as a versatile tool for communication, mission tracking, and player interaction.

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

The PDN is designed to enhance the gameplay experience of participants in the Neotropolis event by providing them with a customizable, portable device that tracks in-game activities, facilitates missions, and engages players through real-time updates and interactions.

These devices are based on an ESP32-S3 microcontroller features wireless communication, LED displays, haptics and an OLED screen. The PDN is central to the Alleycat bounty hunting game's infrastructure, ensuring seamless score tracking, player identification, and interaction with the game world.

## Project Goals

- **Enhanced Player Experience**: Provide users with interactive devices that allow them to participate more deeply in game missions and challenges.
- **Scalability**: Develop a robust framework that supports multiple PDNs operating simultaneously within the game environment.
- **Open Source Collaboration**: Foster collaboration by making the PDN's software and hardware designs available to the community for further improvement.
- **User-Friendly Updates**: Integrate features like **ESP Web Tools** to allow users to easily update their PDNs from their browsers.

## Features

- **Customizable Firmware**: Easily update or modify PDN firmware via a web interface using **ESP Web Tools**.
- **Real-Time Tracking**: Players' progress, scores, and actions are tracked and updated in real time.
- **Built-in Display and LEDs**: The PDN features a screen for player updates and visual indicators for mission progress.
- **Multiplayer Support**: Sync multiple PDNs for group missions and team-based activities.
- **Modular Design**: Designed to accommodate hardware changes, making it adaptable to new game mechanics and hardware innovations.

## Installation

### Prerequisites

- **PlatformIO Core**: Ensure you have PlatformIO installed as it is used for development and flashing the firmware.
- **USB-C Cable**
- **PDN Hardware**: Hardware will be published soon in an accompanying repository.

### Steps

1. Clone the repository:
   ```bash
   git clone https://github.com/AlleycatAssetAcquisitions/PDN-Project.git
   ```

2. Navigate to the project directory:
   ```bash
   cd PDN-Project/pdn-code
   ```

3. Install PlatformIO Core:
   ```bash
   platformio upgrade
   ```

4. Build the project using PlatformIO:
   ```bash
   platformio run
   ```

5. Flash the PDN firmware to your device:
   ```bash
   platformio run --target upload
   ```

## Usage

1. **Power on the PDN**: Once powered, the PDN will connect to the local network and sync with the game server.
2. **Firmware Updates**: Users can update their PDN firmware via the browser using the integrated **ESP Web Tools**.

## Development

### Local Development

If you want to contribute to the PDN Project, follow these steps to set up your local development environment:

1. **Fork the repository** and create your development branch:
   ```bash
   git checkout -b feature/NewFeature
   ```

2. **Develop your feature**, ensuring to follow the project's coding standards.
3. **Commit and push your changes**:
   ```bash
   git commit -m "Add New Feature"
   git push origin feature/NewFeature
   ```

4. **Create a pull request** for review.

### Code Structure

- `include/`: Header files defining key classes and functionality, such as `comms.hpp`, `player.hpp`, and `states.hpp`.
- `src/`: Implementation files like `main.cpp`, `player.cpp`, and `match.cpp`.
- `test/`: Unit testing files for ensuring code stability, including `example_test.cpp`.
- `platformio.ini`: Project configuration file for PlatformIO, defining build environments and dependencies.

## Contributing

We welcome contributions to the PDN project. Whether you have feature suggestions, bug reports, or improvements, please follow our [contributing guidelines](CONTRIBUTING.md).

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
