#pragma once

#include "game/quickdraw-resources.hpp"

/** Metagame event build: single alleycat palette regardless of registration allegiance. */
inline Image getNodeImage(ImageType whichImage) {
    return alleycatImageCollection.at(whichImage);
}
