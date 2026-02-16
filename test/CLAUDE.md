# Testing Conventions

## Test File Convention (CLI tests)

CLI tests use a **split pattern** — test logic lives in `.hpp` headers, TEST_F registrations live in per-domain `.cpp` files:

- **Test functions** go in header files: `test/test_cli/ghost-runner-tests.hpp`, `signal-echo-tests.hpp`, etc.
- **TEST_F registrations** go in per-domain `.cpp` files in `test/test_cli/`

Each domain has its own registration file:

| Registration File | Domain |
|---|---|
| `cli-tests-main.cpp` | `main()` entry point (gtest/gmock) |
| `cli-core-tests.cpp` | Broker, HTTP, Serial, Peer, Button, Light, Display, Command, Role, Reboot |
| `fdn-protocol-tests.cpp` | FDN Protocol, FDN Game, CLI FDN Device, Device Extensions |
| `signal-echo-tests.cpp` | Signal Echo gameplay + difficulty |
| `fdn-integration-tests.cpp` | Integration, Complete, Progress, Challenge, Switching, Color Profile |
| `firewall-decrypt-tests.cpp` | Firewall Decrypt (all sections) |
| `progression-e2e-tests.cpp` | E2E, Re-encounter, Color Context, Bounty Role |
| `ghost-runner-reg-tests.cpp` | Ghost Runner minigame |
| `spike-vector-reg-tests.cpp` | Spike Vector minigame |
| `cipher-path-reg-tests.cpp` | Cipher Path minigame |
| `exploit-seq-reg-tests.cpp` | Exploit Sequencer minigame |
| `breach-defense-reg-tests.cpp` | Breach Defense minigame |

When adding a new test, add entries to **both** the `.hpp` header and the correct `.cpp` registration file:

```cpp
// 1. In ghost-runner-tests.hpp — the test function:
void myNewTestFunction(GhostRunnerTestSuite* suite) {
    // test logic here
}

// 2. In ghost-runner-reg-tests.cpp — the TEST_F registration:
TEST_F(GhostRunnerTestSuite, MyNewTest) {
    myNewTestFunction(this);
}
```

**Important:** Add TEST_F macros to YOUR game's registration file only. Do not edit other games' files.

## Test Structure

```cpp
// GoogleTest framework
TEST_F(TestSuiteName, testDescription) {
    // Arrange
    // Act
    // Assert
}
```

## Core Tests (`test_core/`)

Core tests live in `test/test_core/` and test game logic, state machines, and device abstractions in isolation. They use `device-mock.hpp` for hardware mocking.

## CLI Tests (`test_cli/`)

CLI tests live in `test/test_cli/` and test the full CLI simulator stack — multi-device interaction, serial cable connections, FDN handshakes, and minigame flows. They use `integration-harness.hpp` for device orchestration.
