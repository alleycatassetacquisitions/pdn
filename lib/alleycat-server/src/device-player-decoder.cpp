#include "alleycat-server/device-player-decoder.hpp"
#include "alleycat-server/device_api.pb.h"
#include <pb_decode.h>
#include <algorithm>
#include <cstring>

namespace {

using ResponseMessage = alleycat_device_DevicePlayerResponse;
using AffiliationMessage = alleycat_device_DevicePlayerAffiliation;

static_assert(sizeof(ResponseMessage::id) == UUIDBytes::SIZE,
              "UUID fields in device_api.options must be fixed_length UUIDBytes::SIZE");
static_assert(sizeof(ResponseMessage::team_id) == UUIDBytes::SIZE,
              "UUID fields in device_api.options must be fixed_length UUIDBytes::SIZE");
static_assert(sizeof(AffiliationMessage::category_id) == UUIDBytes::SIZE,
              "UUID fields in device_api.options must be fixed_length UUIDBytes::SIZE");
static_assert(sizeof(AffiliationMessage::affiliation_id) == UUIDBytes::SIZE,
              "UUID fields in device_api.options must be fixed_length UUIDBytes::SIZE");

/** Reads a display name into the char[NAME_BUFFER_SIZE] at @p arg, dropping what does not fit. */
bool decodeDisplayName(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    (void)field;
    char* name = static_cast<char*>(*arg);
    const size_t keptLength = std::min(stream->bytes_left, PlayerLimits::NAME_BUFFER_SIZE - 1);
    if (!pb_read(stream, reinterpret_cast<pb_byte_t*>(name), keptLength)) {
        return false;
    }
    name[keptLength] = '\0';
    return pb_read(stream, nullptr, stream->bytes_left);
}

bool decodeAffiliation(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    (void)field;
    std::vector<Affiliation>* affiliations = static_cast<std::vector<Affiliation>*>(*arg);
    if (affiliations->size() >= PlayerLimits::MAX_AFFILIATIONS) {
        return pb_read(stream, nullptr, stream->bytes_left);
    }

    Affiliation affiliation;
    AffiliationMessage message = alleycat_device_DevicePlayerAffiliation_init_zero;
    message.category_name.funcs.decode = &decodeDisplayName;
    message.category_name.arg = affiliation.categoryName;
    message.affiliation_name.funcs.decode = &decodeDisplayName;
    message.affiliation_name.arg = affiliation.affiliationName;
    if (!pb_decode(stream, alleycat_device_DevicePlayerAffiliation_fields, &message)) {
        return false;
    }

    std::memcpy(affiliation.categoryId.bytes, message.category_id, UUIDBytes::SIZE);
    std::memcpy(affiliation.affiliationId.bytes, message.affiliation_id, UUIDBytes::SIZE);
    if (affiliation.categoryId.isZero() || affiliation.affiliationId.isZero()) {
        return false;
    }
    affiliations->push_back(affiliation);
    return true;
}

}  // namespace

namespace DevicePlayerDecoder {

bool decode(const std::string& body, Player& player) {
    UUIDBytes serverId;
    char serverName[PlayerLimits::NAME_BUFFER_SIZE] = {};
    Team team;
    std::vector<Affiliation> affiliations;

    ResponseMessage message = alleycat_device_DevicePlayerResponse_init_zero;
    message.name.funcs.decode = &decodeDisplayName;
    message.name.arg = serverName;
    message.team_name.funcs.decode = &decodeDisplayName;
    message.team_name.arg = team.name;
    message.affiliations.funcs.decode = &decodeAffiliation;
    message.affiliations.arg = &affiliations;

    pb_istream_t stream = pb_istream_from_buffer(
        reinterpret_cast<const pb_byte_t*>(body.data()), body.size());
    if (!pb_decode(&stream, alleycat_device_DevicePlayerResponse_fields, &message)) {
        return false;
    }

    std::memcpy(serverId.bytes, message.id, UUIDBytes::SIZE);
    if (serverId.isZero()) {
        return false;
    }
    if (message.has_team_id) {
        std::memcpy(team.id.bytes, message.team_id, UUIDBytes::SIZE);
    }
    player.applyRegistrationProfile(serverId, serverName, team, affiliations);
    return true;
}

}  // namespace DevicePlayerDecoder
