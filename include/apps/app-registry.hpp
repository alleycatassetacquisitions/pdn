#pragma once


/*
 * Apps that wish to be loaded by external signals
 * need to be registered here.
*/
enum class AppId {
    PLAYER_REGISTRATION = 100,
    HANDSHAKE = 101,
    QUICKDRAW = 102,
};