#include "alleycat-server/device-player-codec.hpp"
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

/**
 * Takes a record key of exactly RECORD_KEY_SIZE bytes into the RecordKey at
 * @p arg; a field of any other length is skipped and leaves it holding none.
 */
bool decodeRecordKey(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    (void)field;
    RecordKey* recordKey = static_cast<RecordKey*>(*arg);
    // A repeated field's last occurrence wins, as protobuf has it for scalars.
    recordKey->clear();
    if (stream->bytes_left != RECORD_KEY_SIZE) {
        return pb_read(stream, nullptr, stream->bytes_left);
    }
    uint8_t bytes[RECORD_KEY_SIZE];
    const bool wasRead = pb_read(stream, bytes, RECORD_KEY_SIZE);
    if (wasRead) {
        recordKey->set(bytes);
    }
    secureWipe(bytes, sizeof(bytes));
    return wasRead;
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

namespace DevicePlayerCodec {

bool decode(const std::string& body, Player& player, RecordKey& recordKey) {
    recordKey.clear();
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
    // SERVER-SEAM(pdn-record-auth-and-relay): record_key is a provisional field.
    message.record_key.funcs.decode = &decodeRecordKey;
    message.record_key.arg = &recordKey;

    pb_istream_t stream = pb_istream_from_buffer(
        reinterpret_cast<const pb_byte_t*>(body.data()), body.size());
    const bool decoded = pb_decode(&stream, alleycat_device_DevicePlayerResponse_fields, &message);

    std::memcpy(serverId.bytes, message.id, UUIDBytes::SIZE);
    if (!decoded || serverId.isZero()) {
        // The key field can have been read before the response proved invalid.
        recordKey.clear();
        return false;
    }
    if (message.has_team_id) {
        std::memcpy(team.id.bytes, message.team_id, UUIDBytes::SIZE);
    }
    // SERVER-SEAM(fungible-pdns): the three scoring totals are provisional fields.
    // One the response lacks is left at the zero the message started with.
    PlayerTotals totals;
    totals.matchesPlayed = message.scoring_matches_played;
    totals.wins = message.scoring_wins;
    totals.averageDrawTimeMs = message.scoring_average_draw_time_ms;
    player.applyRegistrationProfile(serverId, serverName, team, affiliations, totals);
    return true;
}

}  // namespace DevicePlayerCodec
