#include "alleycat-server/device-player-decoder.hpp"
#include "alleycat-server/device_api.pb.h"
#include <pb_decode.h>
#include <cstring>

namespace {

using ResponseMessage = alleycat_device_DevicePlayerResponse;
using AffiliationMessage = alleycat_device_DevicePlayerAffiliation;

static_assert(sizeof(ResponseMessage::id) == UUIDBytes::TEXT_BUFFER_SIZE,
              "UUID fields in device_api.options must hold UUIDBytes text");
static_assert(sizeof(AffiliationMessage::category_id) == UUIDBytes::TEXT_BUFFER_SIZE,
              "UUID fields in device_api.options must hold UUIDBytes text");
static_assert(sizeof(DevicePlayer::pdnCode) == sizeof(ResponseMessage::pdn_code),
              "DevicePlayerLimits must match device_api.options");
static_assert(sizeof(DevicePlayer::name) == sizeof(ResponseMessage::name),
              "DevicePlayerLimits must match device_api.options");
static_assert(sizeof(DevicePlayer::teamName) == sizeof(ResponseMessage::team_name),
              "DevicePlayerLimits must match device_api.options");
static_assert(sizeof(DevicePlayerAffiliation::categoryName) == sizeof(AffiliationMessage::category_name),
              "DevicePlayerLimits must match device_api.options");
static_assert(sizeof(DevicePlayerAffiliation::affiliationName) ==
                  sizeof(AffiliationMessage::affiliation_name),
              "DevicePlayerLimits must match device_api.options");

bool copyAffiliation(const AffiliationMessage& message, DevicePlayerAffiliation& affiliation) {
    if (!UUIDBytes::parse(message.category_id, affiliation.categoryId) ||
        !UUIDBytes::parse(message.affiliation_id, affiliation.affiliationId)) {
        return false;
    }
    std::memcpy(affiliation.categoryName, message.category_name, sizeof(affiliation.categoryName));
    std::memcpy(affiliation.affiliationName, message.affiliation_name, sizeof(affiliation.affiliationName));
    return true;
}

bool copyPlayerHeader(const ResponseMessage& message, DevicePlayer& player) {
    if (!UUIDBytes::parse(message.id, player.id) || !UUIDBytes::parse(message.event_id, player.eventId)) {
        return false;
    }
    if (message.has_team_id && !UUIDBytes::parse(message.team_id, player.teamId)) {
        return false;
    }
    std::memcpy(player.pdnCode, message.pdn_code, sizeof(player.pdnCode));
    std::memcpy(player.name, message.name, sizeof(player.name));
    if (message.has_team_name) {
        std::memcpy(player.teamName, message.team_name, sizeof(player.teamName));
    }
    player.createdAtUnixMs = message.created_at_unix_ms;
    player.active = message.active;
    return true;
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
    if (!copyAffiliation(message, affiliation)) {
        return false;
    }
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

    if (!copyPlayerHeader(message, decoded)) {
        return false;
    }
    player = decoded;
    return true;
}

}  // namespace DevicePlayerDecoder
