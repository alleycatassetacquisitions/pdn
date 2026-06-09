#pragma once

// These values are the wire encoding: transmitted on serial in the HELLO frame
// (opcode 0x00), not just internal ids. Don't renumber or reorder existing
// values; append new ones at the end.
enum class DeviceType {
    UNKNOWN = 0,
    PDN = 1,
    FDN = 2,
};