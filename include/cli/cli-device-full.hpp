#pragma once

#ifdef NATIVE_BUILD

/*
 * Convenience header that provides cli-device.hpp plus all the heavy
 * driver/game includes that were historically part of cli-device.hpp.
 *
 * Used by test code and cli-main.cpp that need the full type definitions.
 * Production command handlers should prefer cli-device-types.hpp instead.
 */

#include "cli/cli-device.hpp"

// Full driver includes
#include "device/drivers/native/native-logger-driver.hpp"
#include "device/drivers/native/native-clock-driver.hpp"
#include "device/drivers/native/native-display-driver.hpp"
#include "device/drivers/native/native-button-driver.hpp"
#include "device/drivers/native/native-light-strip-driver.hpp"
#include "device/drivers/native/native-haptics-driver.hpp"
#include "device/drivers/native/native-serial-driver.hpp"
#include "device/drivers/native/native-http-client-driver.hpp"
#include "device/drivers/native/native-peer-comms-driver.hpp"
#include "device/drivers/native/native-prefs-driver.hpp"

// Core game components
#include "device/pdn.hpp"
#include "game/player.hpp"
#include "game/quickdraw.hpp"
#include "game/fdn-game.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"
#include "game/firewall-decrypt/firewall-decrypt.hpp"
#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt-resources.hpp"
#include "game/ghost-runner/ghost-runner.hpp"
#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/spike-vector/spike-vector.hpp"
#include "game/spike-vector/spike-vector-states.hpp"
#include "game/cipher-path/cipher-path.hpp"
#include "game/cipher-path/cipher-path-states.hpp"
#include "game/exploit-sequencer/exploit-sequencer.hpp"
#include "game/exploit-sequencer/exploit-sequencer-states.hpp"
#include "game/breach-defense/breach-defense.hpp"
#include "game/breach-defense/breach-defense-states.hpp"
#include "game/konami-metagame/konami-metagame.hpp"
#include "wireless/quickdraw-wireless-manager.hpp"
#include "wireless/peer-comms-types.hpp"

// CLI components
#include "cli/cli-serial-broker.hpp"
#include "cli/cli-http-server.hpp"

#endif // NATIVE_BUILD
