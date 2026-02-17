#ifdef NATIVE_BUILD

#include "cli/cli-device-types.hpp"
#include "game/signal-echo/signal-echo-states.hpp"
#include "game/firewall-decrypt/firewall-decrypt-states.hpp"
#include "game/ghost-runner/ghost-runner-states.hpp"
#include "game/spike-vector/spike-vector-states.hpp"
#include "game/cipher-path/cipher-path-states.hpp"
#include "game/exploit-sequencer/exploit-sequencer-states.hpp"
#include "game/breach-defense/breach-defense-states.hpp"

namespace cli {

const char* getQuickdrawStateName(int stateId) {
    switch (stateId) {
        case 0:  return "PlayerRegistration";
        case 1:  return "FetchUserData";
        case 2:  return "ConfirmOffline";
        case 3:  return "ChooseRole";
        case 4:  return "AllegiancePicker";
        case 5:  return "WelcomeMessage";
        case 6:  return "Sleep";
        case 7:  return "AwakenSequence";
        case 8:  return "Idle";
        case 9:  return "HandshakeInitiate";
        case 10: return "BountySendCC";
        case 11: return "HunterSendId";
        case 12: return "ConnectionSuccessful";
        case 13: return "DuelCountdown";
        case 14: return "Duel";
        case 15: return "DuelPushed";
        case 16: return "DuelReceivedResult";
        case 17: return "DuelResult";
        case 18: return "Win";
        case 19: return "Lose";
        case 20: return "UploadMatches";
        case 21: return "FdnDetected";
        case 22: return "FdnComplete";
        case 23: return "ColorProfilePrompt";
        case 24: return "ColorProfilePicker";
        case 25: return "FdnReencounter";
        case 26: return "KonamiPuzzle";
        default: return "Unknown";
    }
}

const char* getSignalEchoStateName(int stateId) {
    switch (stateId) {
        case ECHO_INTRO:         return "EchoIntro";
        case ECHO_SHOW_SEQUENCE: return "EchoShowSequence";
        case ECHO_PLAYER_INPUT:  return "EchoPlayerInput";
        case ECHO_EVALUATE:      return "EchoEvaluate";
        case ECHO_WIN:           return "EchoWin";
        case ECHO_LOSE:          return "EchoLose";
        default:                 return "Unknown";
    }
}

const char* getFirewallDecryptStateName(int stateId) {
    switch (stateId) {
        case DECRYPT_INTRO:    return "DecryptIntro";
        case DECRYPT_SCAN:     return "DecryptScan";
        case DECRYPT_EVALUATE: return "DecryptEvaluate";
        case DECRYPT_WIN:      return "DecryptWin";
        case DECRYPT_LOSE:     return "DecryptLose";
        default:               return "Unknown";
    }
}

const char* getGhostRunnerStateName(int stateId) {
    switch (stateId) {
        case GHOST_INTRO:    return "GhostRunnerIntro";
        case GHOST_WIN:      return "GhostRunnerWin";
        case GHOST_LOSE:     return "GhostRunnerLose";
        case GHOST_SHOW:     return "GhostRunnerShow";
        case GHOST_GAMEPLAY: return "GhostRunnerGameplay";
        case GHOST_EVALUATE: return "GhostRunnerEvaluate";
        default:             return "Unknown";
    }
}

const char* getSpikeVectorStateName(int stateId) {
    switch (stateId) {
        case SPIKE_INTRO:    return "SpikeVectorIntro";
        case SPIKE_WIN:      return "SpikeVectorWin";
        case SPIKE_LOSE:     return "SpikeVectorLose";
        case SPIKE_SHOW:     return "SpikeVectorShow";
        case SPIKE_GAMEPLAY: return "SpikeVectorGameplay";
        case SPIKE_EVALUATE: return "SpikeVectorEvaluate";
        default:             return "Unknown";
    }
}

const char* getCipherPathStateName(int stateId) {
    switch (stateId) {
        case CIPHER_INTRO:    return "CipherPathIntro";
        case CIPHER_WIN:      return "CipherPathWin";
        case CIPHER_LOSE:     return "CipherPathLose";
        case CIPHER_SHOW:     return "CipherPathShow";
        case CIPHER_GAMEPLAY: return "CipherPathGameplay";
        case CIPHER_EVALUATE: return "CipherPathEvaluate";
        default:              return "Unknown";
    }
}

const char* getExploitSequencerStateName(int stateId) {
    switch (stateId) {
        case EXPLOIT_INTRO:    return "ExploitSeqIntro";
        case EXPLOIT_WIN:      return "ExploitSeqWin";
        case EXPLOIT_LOSE:     return "ExploitSeqLose";
        case EXPLOIT_SHOW:     return "ExploitSeqShow";
        case EXPLOIT_GAMEPLAY: return "ExploitSeqGameplay";
        case EXPLOIT_EVALUATE: return "ExploitSeqEvaluate";
        default:               return "Unknown";
    }
}

const char* getBreachDefenseStateName(int stateId) {
    switch (stateId) {
        case BREACH_INTRO:    return "BreachDefenseIntro";
        case BREACH_WIN:      return "BreachDefenseWin";
        case BREACH_LOSE:     return "BreachDefenseLose";
        case BREACH_SHOW:     return "BreachDefenseShow";
        case BREACH_GAMEPLAY: return "BreachDefenseGameplay";
        case BREACH_EVALUATE: return "BreachDefenseEvaluate";
        default:              return "Unknown";
    }
}

const char* getFdnStateName(int stateId) {
    switch (stateId) {
        case 0: return "NpcIdle";
        case 1: return "NpcHandshake";
        case 2: return "NpcGameActive";
        case 3: return "NpcReceiveResult";
        default: return "Unknown";
    }
}

const char* getStateName(int stateId, DeviceType deviceType, GameType gameType) {
    if (deviceType == DeviceType::FDN) {
        return getFdnStateName(stateId);
    }
    if (stateId >= BREACH_INTRO) {
        return getBreachDefenseStateName(stateId);
    }
    if (stateId >= EXPLOIT_INTRO) {
        return getExploitSequencerStateName(stateId);
    }
    if (stateId >= CIPHER_INTRO) {
        return getCipherPathStateName(stateId);
    }
    if (stateId >= SPIKE_INTRO) {
        return getSpikeVectorStateName(stateId);
    }
    if (stateId >= GHOST_INTRO) {
        return getGhostRunnerStateName(stateId);
    }
    if (stateId >= DECRYPT_INTRO) {
        return getFirewallDecryptStateName(stateId);
    }
    if (stateId >= ECHO_INTRO) {
        return getSignalEchoStateName(stateId);
    }
    return getQuickdrawStateName(stateId);
}

} // namespace cli

#endif // NATIVE_BUILD
