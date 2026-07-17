#pragma once

#include "utils/simple-timer.hpp"
#include "game/floaty-boat-glyphs.hpp"
#include <algorithm>
#include <cstdlib>
#include <vector>

struct Barrier {
    const char* glyph = FloatyBoatGlyphs::kBarrier;
    int lane = 0;
    int xCoord = 0;
};

class BarrierController {
public:
    enum class Mode {
        NORMAL,
        EASY,  // slow, single barriers, no speed-up — for tutorial practice
    };

    BarrierController() = default;
    ~BarrierController() = default;

    void start(Mode mode = Mode::NORMAL) {
        mode_ = mode;
        frozen_ = false;
        barriers_.clear();
        barrierSpeedUpCounter_ = 0;

        if (mode_ == Mode::EASY) {
            barrierMovementInterval_ = 60.0f;
            barrierSpeedUpInterval_ = 9999;
            nextSpeedUpInterval_ = 9999;
            nextBarrierSpawnRange_[0] = 500;
            nextBarrierSpawnRange_[1] = 400;
            minSpawnDistance_ = 55;
            spawnDistanceVariance_ = 30;
        } else {
            barrierMovementInterval_ = 80.0f;
            barrierSpeedUpInterval_ = 2;
            nextSpeedUpInterval_ = 3;
            nextBarrierSpawnRange_[0] = 100;
            nextBarrierSpawnRange_[1] = 350;
            minSpawnDistance_ = 40;
            spawnDistanceVariance_ = 40;
        }

        barrierSpawnTimer_.setTimer(
            static_cast<unsigned long>(std::rand() % nextBarrierSpawnRange_[1] + nextBarrierSpawnRange_[0]));
        barrierMovementTimer_.setTimer(static_cast<unsigned long>(barrierMovementInterval_));
        addNewBarrier();
    }

    void freeze() {
        frozen_ = true;
    }

    const std::vector<Barrier>& getBarriers() const {
        return barriers_;
    }

    void updateMovementAndSpawns() {
        if (frozen_) {
            return;
        }

        if (barrierMovementTimer_.expired()) {
            for (auto& barrier : barriers_) {
                barrier.xCoord--;
            }
            barrierMovementTimer_.setTimer(static_cast<unsigned long>(barrierMovementInterval_));
        }

        if (barrierSpawnTimer_.expired()) {
            addNewBarrier();
            barrierSpawnTimer_.setTimer(
                static_cast<unsigned long>(std::rand() % nextBarrierSpawnRange_[1] + nextBarrierSpawnRange_[0]));

            barrierSpeedUpCounter_++;
            if (barrierSpeedUpCounter_ >= barrierSpeedUpInterval_) {
                barrierSpeedUpCounter_ = 0;
                barrierMovementInterval_ =
                    std::max(minMovementInterval_, barrierMovementInterval_ * speedUpFactor_);
                int next = std::min(13, barrierSpeedUpInterval_ + nextSpeedUpInterval_);
                barrierSpeedUpInterval_ = std::min(13, nextSpeedUpInterval_);
                nextSpeedUpInterval_ = next;
            }
        }
    }

    // Awards +1 per obstacle column that has fully passed the ship.
    int removeClearedColumns() {
        int columnsCleared = 0;
        std::vector<int> clearedXs;

        for (const auto& barrier : barriers_) {
            if (barrier.xCoord > kDespawnX) {
                continue;
            }
            if (std::find(clearedXs.begin(), clearedXs.end(), barrier.xCoord) == clearedXs.end()) {
                clearedXs.push_back(barrier.xCoord);
                columnsCleared++;
            }
        }

        barriers_.erase(
            std::remove_if(
                barriers_.begin(),
                barriers_.end(),
                [](const Barrier& b) { return b.xCoord <= kDespawnX; }),
            barriers_.end());

        return columnsCleared;
    }

private:
    void addNewBarrier() {
        int spawnX = 120;
        if (!barriers_.empty()) {
            auto mostRecent = std::max_element(
                barriers_.begin(),
                barriers_.end(),
                [](const Barrier& a, const Barrier& b) { return a.xCoord < b.xCoord; });
            int gap = minSpawnDistance_ + (std::rand() % spawnDistanceVariance_);
            spawnX = std::max(120, mostRecent->xCoord + gap);
        }

        Barrier barrier;
        barrier.lane = std::rand() % 3;
        barrier.xCoord = spawnX;
        barriers_.push_back(barrier);

        // Normal mode: 50% chance of a second barrier (always leaves one safe lane).
        if (mode_ == Mode::NORMAL && std::rand() % 2 == 0) {
            Barrier second;
            second.lane = std::rand() % 3;
            while (second.lane == barrier.lane) {
                second.lane = std::rand() % 3;
            }
            second.xCoord = spawnX;
            barriers_.push_back(second);
        }
    }

    std::vector<Barrier> barriers_;
    Mode mode_ = Mode::NORMAL;

    SimpleTimer barrierMovementTimer_;
    float barrierMovementInterval_ = 80.0f;
    static constexpr float speedUpFactor_ = 0.75f;
    static constexpr float minMovementInterval_ = 5.0f;

    SimpleTimer barrierSpawnTimer_;
    int nextBarrierSpawnRange_[2] = {100, 350};

    int barrierSpeedUpInterval_ = 2;
    int nextSpeedUpInterval_ = 3;
    int barrierSpeedUpCounter_ = 0;

    int minSpawnDistance_ = 40;
    int spawnDistanceVariance_ = 40;
    static constexpr int kDespawnX = 12;

    bool frozen_ = false;
};
