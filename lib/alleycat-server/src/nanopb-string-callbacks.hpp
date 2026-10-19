#pragma once

#include <pb.h>
#include <pb_decode.h>
#include <string>
#include <vector>

namespace NanopbStringCallbacks {

/** Decodes a string field into the std::string at *arg. */
inline bool decodeString(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    (void)field;
    std::string* target = static_cast<std::string*>(*arg);
    target->resize(stream->bytes_left);
    return pb_read(stream, reinterpret_cast<pb_byte_t*>(&(*target)[0]), stream->bytes_left);
}

/** Appends one element of a repeated string field to the std::vector<std::string> at *arg. */
inline bool appendString(pb_istream_t* stream, const pb_field_t* field, void** arg) {
    std::vector<std::string>* targets = static_cast<std::vector<std::string>*>(*arg);
    targets->emplace_back();
    void* target = &targets->back();
    return decodeString(stream, field, &target);
}

/** Routes a callback string field into @p target. */
inline void bindString(pb_callback_t& callback, std::string& target) {
    callback.funcs.decode = &decodeString;
    callback.arg = &target;
}

}  // namespace NanopbStringCallbacks
