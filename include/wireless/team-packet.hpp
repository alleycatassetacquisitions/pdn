#pragma once

#include <cstdint>
#include <cstring>
#include "wireless/peer-comms-types.hpp"

enum class TeamCommandType : uint8_t { REGISTER = 0, DEREGISTER = 1, CONFIRM = 2, GAME_EVENT = 3, REGISTER_INVITE = 4, INVITE_REJECT = 5 };
enum class GameEventType : uint8_t { COUNTDOWN = 0, DRAW = 1, WIN = 2, LOSS = 3 };

// Wire format: 2 bytes. For GAME_EVENT, eventType is GameEventType.
// For REGISTER/DEREGISTER/CONFIRM, eventType carries chain position.
struct TeamPacket {
    uint8_t commandType;
    uint8_t eventType;
} __attribute__((packed));

inline void sendTeamPacket(PeerCommsInterface* peerComms, const uint8_t* dst,
                           TeamCommandType cmd, uint8_t payload = 0) {
    TeamPacket pkt{static_cast<uint8_t>(cmd), payload};
    peerComms->sendData(dst, PktType::kTeamCommand,
        reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}

static constexpr int CHAMPION_NAME_MAX = 16;

// REGISTER_INVITE carries the champion MAC and name so forwarded invites preserve the original champion.
struct RegisterInvitePacket {
    uint8_t commandType;
    uint8_t position;
    uint8_t championIsHunter;
    uint8_t championMac[6];
    char championName[CHAMPION_NAME_MAX];
} __attribute__((packed));

inline void sendRegisterInvite(PeerCommsInterface* peerComms, const uint8_t* dst,
                               uint8_t position, bool championIsHunter,
                               const uint8_t* championMac, const char* championName) {
    RegisterInvitePacket pkt{static_cast<uint8_t>(TeamCommandType::REGISTER_INVITE), position,
        static_cast<uint8_t>(championIsHunter ? 1 : 0), {}, {}};
    memcpy(pkt.championMac, championMac, 6);
    strncpy(pkt.championName, championName, CHAMPION_NAME_MAX - 1);
    pkt.championName[CHAMPION_NAME_MAX - 1] = '\0';
    peerComms->sendData(dst, PktType::kTeamCommand,
        reinterpret_cast<const uint8_t*>(&pkt), sizeof(pkt));
}

inline void broadcastTeamEvent(PeerCommsInterface* peerComms, GameEventType evt) {
    sendTeamPacket(peerComms, peerComms->getGlobalBroadcastAddress(),
                   TeamCommandType::GAME_EVENT, static_cast<uint8_t>(evt));
}
