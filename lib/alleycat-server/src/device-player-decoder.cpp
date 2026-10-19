#include "alleycat-server/device-player-decoder.hpp"
#include "alleycat-server/device_api.pb.h"
#include "nanopb-string-callbacks.hpp"
#include <pb_decode.h>
#include <utility>

namespace {

bool decodeAffiliation(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    (void)field;
    std::vector<DevicePlayerAffiliation>* affiliations =
        static_cast<std::vector<DevicePlayerAffiliation>*>(*arg);

    DevicePlayerAffiliation affiliation;
    alleycat_device_DevicePlayerAffiliation message = alleycat_device_DevicePlayerAffiliation_init_zero;
    NanopbStringCallbacks::bindString(message.category_id, affiliation.categoryId);
    NanopbStringCallbacks::bindString(message.category_name, affiliation.categoryName);
    NanopbStringCallbacks::bindString(message.affiliation_id, affiliation.affiliationId);
    NanopbStringCallbacks::bindString(message.affiliation_name, affiliation.affiliationName);

    if (!pb_decode(stream, alleycat_device_DevicePlayerAffiliation_fields, &message)) {
        return false;
    }
    affiliations->push_back(std::move(affiliation));
    return true;
}

}  // namespace

namespace DevicePlayerDecoder {

bool decode(const std::string& body, DevicePlayer& player) {
    player = DevicePlayer();

    alleycat_device_DevicePlayerResponse message = alleycat_device_DevicePlayerResponse_init_zero;
    NanopbStringCallbacks::bindString(message.id, player.id);
    NanopbStringCallbacks::bindString(message.event_id, player.eventId);
    NanopbStringCallbacks::bindString(message.pdn_code, player.pdnCode);
    NanopbStringCallbacks::bindString(message.name, player.name);
    NanopbStringCallbacks::bindString(message.team_id, player.teamId);
    NanopbStringCallbacks::bindString(message.team_name, player.teamName);
    message.affiliations.funcs.decode = &decodeAffiliation;
    message.affiliations.arg = &player.affiliations;

    pb_istream_t stream = pb_istream_from_buffer(
        reinterpret_cast<const pb_byte_t*>(body.data()), body.size());
    if (!pb_decode(&stream, alleycat_device_DevicePlayerResponse_fields, &message)) {
        return false;
    }

    player.createdAtUnixMs = message.created_at_unix_ms;
    player.active = message.active;
    return true;
}

}  // namespace DevicePlayerDecoder
