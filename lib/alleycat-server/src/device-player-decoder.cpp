#include "alleycat-server/device-player-decoder.hpp"
#include "alleycat-server/device_api.pb.h"
#include <pb_decode.h>

namespace {

using ResponseMessage = alleycat_device_DevicePlayerResponse;
using AffiliationMessage = alleycat_device_DevicePlayerAffiliation;

static_assert(sizeof(DevicePlayer::id) == sizeof(ResponseMessage::id),
              "DevicePlayer must match device_api.options");
static_assert(sizeof(DevicePlayer::pdnCode) == sizeof(ResponseMessage::pdn_code),
              "DevicePlayerLimits must match device_api.options");
static_assert(sizeof(DevicePlayer::name) == sizeof(ResponseMessage::name),
              "DevicePlayerLimits must match device_api.options");
static_assert(sizeof(DevicePlayer::teamName) == sizeof(ResponseMessage::team_name),
              "DevicePlayerLimits must match device_api.options");
static_assert(sizeof(DevicePlayerAffiliation::categoryId) == sizeof(AffiliationMessage::category_id),
              "DevicePlayerAffiliation must match device_api.options");
static_assert(sizeof(DevicePlayerAffiliation::categoryName) == sizeof(AffiliationMessage::category_name),
              "DevicePlayerLimits must match device_api.options");
static_assert(sizeof(DevicePlayerAffiliation::affiliationName) ==
                  sizeof(AffiliationMessage::affiliation_name),
              "DevicePlayerLimits must match device_api.options");

void copyAffiliation(const AffiliationMessage& message, DevicePlayerAffiliation& affiliation) {
    IdGenerator::copyId(affiliation.categoryId, message.category_id);
    IdGenerator::copyId(affiliation.categoryName, message.category_name);
    IdGenerator::copyId(affiliation.affiliationId, message.affiliation_id);
    IdGenerator::copyId(affiliation.affiliationName, message.affiliation_name);
}

void copyPlayerHeader(const ResponseMessage& message, DevicePlayer& player) {
    IdGenerator::copyId(player.id, message.id);
    IdGenerator::copyId(player.eventId, message.event_id);
    IdGenerator::copyId(player.pdnCode, message.pdn_code);
    IdGenerator::copyId(player.name, message.name);
    if (message.has_team_id) {
        IdGenerator::copyId(player.teamId, message.team_id);
    }
    if (message.has_team_name) {
        IdGenerator::copyId(player.teamName, message.team_name);
    }
    player.createdAtUnixMs = message.created_at_unix_ms;
    player.active = message.active;
}

bool decodeAffiliation(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    (void)field;
    std::vector<DevicePlayerAffiliation>* affiliations =
        static_cast<std::vector<DevicePlayerAffiliation>*>(*arg);
    if (affiliations->size() >= DevicePlayerLimits::MAX_AFFILIATIONS) {
        return false;
    }

    AffiliationMessage message = alleycat_device_DevicePlayerAffiliation_init_zero;
    if (!pb_decode(stream, alleycat_device_DevicePlayerAffiliation_fields, &message)) {
        return false;
    }
    DevicePlayerAffiliation affiliation;
    copyAffiliation(message, affiliation);
    affiliations->push_back(affiliation);
    return true;
}

}  // namespace

namespace DevicePlayerDecoder {

bool decode(const std::string& body, DevicePlayer& player) {
    DevicePlayer decoded;

    ResponseMessage message = alleycat_device_DevicePlayerResponse_init_zero;
    message.affiliations.funcs.decode = &decodeAffiliation;
    message.affiliations.arg = &decoded.affiliations;

    pb_istream_t stream = pb_istream_from_buffer(
        reinterpret_cast<const pb_byte_t*>(body.data()), body.size());
    if (!pb_decode(&stream, alleycat_device_DevicePlayerResponse_fields, &message)) {
        return false;
    }

    copyPlayerHeader(message, decoded);
    player = decoded;
    return true;
}

}  // namespace DevicePlayerDecoder
