#pragma once

#include "image.hpp"
#include "images-raw.hpp"

// Shared quickdraw duel images (alleycat art) for FDN and other non-PDN builds.
inline Image quickdrawImageFor(ImageType type) {
    switch (type) {
        case ImageType::COUNTDOWN_THREE:
            return Image(image_alley_count3, 128, 64, 0, 0);
        case ImageType::COUNTDOWN_TWO:
            return Image(image_alley_count2, 128, 64, 0, 0);
        case ImageType::COUNTDOWN_ONE:
            return Image(image_alley_count1, 128, 64, 0, 0);
        case ImageType::DRAW:
            return Image(image_draw, 128, 64, 64, 0);
        case ImageType::WIN:
            return Image(image_alley_victor, 64, 64, 64, 0);
        case ImageType::LOSE:
            return Image(image_alley_loser, 64, 64, 64, 0);
        default:
            return Image(image_alley_0, 128, 64, 0, 0);
    }
}
