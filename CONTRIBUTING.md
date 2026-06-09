### Introduction

Thank you for your interest in contributing to the **Portable Data Node (PDN)** projects. Alleycat Asset Acquisitions (AAA) is excited to involve passionate community members in helping build an immersive, high-octane gaming experience, particularly with the technological integration of the PDNs into LARP events like Neotropolis. Whether you're here to suggest new content, help with development, or contribute ideas for our Cyberpunk LARP universe, this guide will help you understand how to contribute effectively.

### How You Can Contribute

1. **Code Contributions**: Help improve the PDN software by contributing to the firmware, creating new features, optimizing the codebase, or squashing bugs.
2. **Hardware Enhancements**: Suggest and implement new hardware integrations that can expand the capabilities of the PDN devices.
3. **Firmware Updates & Testing**: Assist in testing and debugging the PDN firmware on the ESP32-S3 platform and help develop user-friendly update processes.
4. **Documentation**: Help expand and improve the project’s documentation, making it easier for new contributors to get started and users to manage their PDNs.
5. **Feature Suggestions & Bug Reports**: Provide feedback and ideas for enhancing the PDN’s functionality in the game, or report any bugs you encounter.
6. **Game Mechanics & Rules Development**: Propose new gameplay mechanics that can be integrated into the PDN software, ensuring that they are balanced and enhance the player experience.

### Getting Started

Before contributing, please ensure that you follow these steps:

1. **Familiarize Yourself with the Project**: Review the codebase in the repository and the project’s documentation (including this file, the `README.md`, and any relevant `wiki` pages). Understanding how the PDN firmware works will help you contribute more effectively.
   
2. **Set Up Your Development Environment**: Follow the instructions in the `README.md` to set up PlatformIO and any necessary dependencies on your local machine. Ensure you can build and flash the firmware onto an ESP32-S3 device.

3. **Join Our Community**: [Join us on Discord!](https://discord.gg/6XGTCbKkUy)

### Architecture in Practice

End to end, a connection flows like this: plugging a cable starts the 20ms HELLO stream (`RemoteDeviceCoordinator`), and receiving one registers the sender as that jack's direct peer. Each device floods a BEACON naming its own two direct peers; the peer-graph (`lib/core/include/device/peer-graph.hpp`) turns those into mutual-consistency edges, from which chain membership, ring closure, and the champion are derived on demand (`chain_role::` in `src/pdn/game/chain-manager.cpp`). Supporters confirm to their champion for boost (`ChainManager`), duels run through `MatchManager`'s reliable channel, and when the graph shows a closed ring, `ShootoutManager` elects a coordinator and runs the bracket. All topology rides serial; all game traffic rides `WirelessTransport` channels. When debugging, find your symptom's layer in that sentence and start in the file that owns it.

- **`AppConfig` only contains Quickdraw.** Chain Duel, Shootout, and Symbol-Match are sub-states of Quickdraw, not sibling apps. See `Quickdraw::populateStateMap()`. They all funnel into the same match machinery (`MatchManager`, the duel states), so a game mode is a state transition inside one app; sibling apps would each need their own copy of that plumbing and their own wiring to the wireless channels.
- **RDC owns the serial link.** `RemoteDeviceCoordinator` wires a `SerialFrameParser` per jack in `initialize()` and is the only consumer of serial traffic. Connectivity tracking deliberately runs below game state: games mount and dismount while the jacks keep being watched, so topology survives every screen change, which only holds if exactly one component consumes serial. Two binary frames carry everything: HELLO (20ms liveness + peer identity: MAC, `DeviceType`, userId) and BEACON (flooded topology). Read peer info via `getPeerMac()` / `getPeerDeviceType()` / `getPeerUserId()`; don't write your own serial protocol.
- **`SerialManager` callbacks are single-slot per jack, owned by RDC.** `setOnBytesReceivedCallback()` overwrites; calling it from game code silently replaces RDC's parser and kills connectivity detection. Application messages go over ESP-NOW, never serial.
- **Extending the serial protocol** (connectivity frames only): the wire format lives in `lib/core/include/device/peer-graph-codec.hpp` (preamble + opcode + fixed-length payload + CRC-16; one opcode constant, payload length, and encode/decode pair per frame). A new frame also needs a case in `SerialFrameParser::opcodePayloadLen()` and handling in RDC's frame handler (`ingestSerial`, which only validates and enqueues; graph mutation happens on the main loop). Serial has no ACK/retry layer and the jacks drop frames routinely, so frames must be idempotent repeated announcements, and nothing may starve the 20ms HELLO stream (use the paced replay queue for anything bursty).
- **`WirelessTransport` / `ReliableChannel` is the only sanctioned ESP-NOW send path.** Raw ESP-NOW is fire-and-forget; delivery confirmation, retry, dedup, and abandonment live in the transport once so every game mode shares one machinery instead of growing per-feature copies that fight over ack and sequence space. The header comment in `lib/core/include/wireless/wireless-transport.hpp` documents the full reliable-packet lifecycle (send, retransmit, ack, dedup, abandonment); `lib/core/include/device/drivers/peer-comms-types.hpp` is the packet-type registry (`PktType`). The platform loop owns `transport->sync()`; game managers must never pump it. Abandon callbacks fire inline during `sync()`; treat abandonment as a game-level signal (void the match, abort the tournament), and keep the callback cheap.
- **Adding a wireless packet type**: add a `PktType` value, define a fixed-layout payload struct, claim a channel via `transport->channel<Payload>(type, onAbandon)` in your manager, and wire `onReceive`; claiming the channel is what makes its receive path live. `MatchManager` (`src/pdn/game/match-manager.cpp`, `kQuickdrawCommand`) is the exemplar. Two constraints the compiler won't catch: a `PktType` must be either fully sub-typed or have a single subType-0 channel, never both (byte-0 routing would silently misroute plain payloads whose first byte collides with a sibling's cmd); and payloads must fit one ESP-NOW frame (~246 bytes).

### Contribution Workflow

1. **Propose Your Contribution**: Submit a brief outline of your contribution idea or feature request via GitHub issues.

2. **Develop Locally**: Set up your development environment using PlatformIO (see the `README.md` for details) and develop your feature or bug fix locally. Be sure to follow coding standards and best practices outlined in the project.

3. **Test Your Changes**: Test your changes thoroughly by flashing the firmware to a compatible microcontroller (ESP32-s3) and ensuring it works as intended in the game environment. Make use of the test files in the `test/` folder.

4. **Submit a Pull Request**: Once your contribution is ready, commit your changes and push them to your fork. Open a pull request (PR) on the main repository for review. Provide a clear description of the changes made and link to any related issues. **Note**: Do not commit `wifi_credentials.ini` - the CI/CD pipeline automatically creates this file with dummy values for automated builds.

5. **Code Review & Feedback**: A maintainer will review your PR and may provide feedback or request changes. Be open to collaborating and making any necessary adjustments to ensure your contribution aligns with the project’s standards.

6. **Approval & Integration**: Once your changes are approved, they will be merged into the main branch and may be included in a future release.

### Standards & Requirements

- **Originality**: All contributions must be original and not violate any copyright laws. Do not submit plagiarized content.
   
- **Coding Guidelines**: Follow the coding standards in the repository. Use meaningful variable names, document your code where appropriate, and ensure your changes do not introduce unnecessary complexity.
   
- **Testing**: Any code contributions should be accompanied by tests to ensure functionality. Use the files in the `test/` folder as a guide for writing new test cases.
   
- **Collaboration**: Be respectful and open to feedback when collaborating with other contributors. We are all working toward the same goal of improving the PDN experience.

### Bug Reporting

If you encounter any issues with the PDN software, here’s how you can report bugs effectively:

- Provide a clear and detailed description of the bug, including the firmware version and any hardware details.
- Include steps to reproduce the issue and any logs from PlatformIO or other relevant sources.
- Attach any relevant screenshots or video clips, if applicable.
- Suggest possible solutions or workarounds, if you have any ideas.

### Code of Conduct

All contributors are expected to adhere to the following standards:

- **Be Respectful**: Treat others with kindness and professionalism.
- **Constructive Feedback**: When providing feedback, aim to be constructive and helpful.
- **Inclusive Environment**: We are committed to providing a welcoming space for everyone, regardless of background, gender, ethnicity, or experience level.

### Licensing & Ownership

By submitting your contribution, you agree that Alleycat Asset Acquisitions has the right to use, modify, and distribute your content in any form within the PDN Project. You will retain credit as the original creator.
