#pragma once

#include "utils/simple-timer.hpp"
#include <vector>
#include "apps/speeder/speeder-app-resources.hpp"
#include <random>
#include <algorithm>

static int barrierIDCounter = 0;
    
struct Barrier {
    const char* glyph = barrier;
    int lane = 0;
    int xCoord = 0;
    int barrierID = barrierIDCounter++;

    bool operator==(const Barrier& other) const {
        return barrierID == other.barrierID;
    }
};

class BarrierController {

public:
    BarrierController() {};
    ~BarrierController() = default;

    void start() {
        frozen = false;
        barriers.clear();
        barrierMovementInterval = 80.0f;
        barrierSpeedUpInterval = 2;
        nextSpeedUpInterval = 3;
        barrierSpeedUpCounter = 0;
        barrierSpawnTimer.setTimer(std::rand() % nextBarrierSpawnRange[1] + nextBarrierSpawnRange[0]);
        barrierMovementTimer.setTimer((unsigned long)barrierMovementInterval);
        addNewBarrier();
    }

    void freeze() {
        frozen = true;
    }

    const std::vector<Barrier>& getBarriers() const {
        return barriers;
    }

    void addNewBarrier() {
        int spawnX = 120;
        if(!barriers.empty()) {
            auto mostRecent = std::max_element(barriers.begin(), barriers.end(),
                [](const Barrier& a, const Barrier& b) { return a.xCoord < b.xCoord; });
            int gap = minSpawnDistance + (std::rand() % spawnDistanceVariance);
            spawnX = std::max(120, mostRecent->xCoord + gap);
        }

        Barrier barrier;
        barrier.lane = std::rand() % 3;
        barrier.xCoord = spawnX;
        barriers.push_back(barrier);

        if(std::rand() % 2 == 0) {
            Barrier second;
            second.lane = std::rand() % 3;
            while(second.lane == barrier.lane) {
                second.lane = std::rand() % 3;
            }
            second.xCoord = spawnX;
            barriers.push_back(second);
        }
    }
    
    const std::vector<Barrier>& updateBarriers() {
        if(!frozen) {
            if(barrierMovementTimer.expired()) {
                for(auto& barrier : barriers) {
                    barrier.xCoord--;
                }
                barrierMovementTimer.setTimer((unsigned long)barrierMovementInterval);
            }

            if(barrierSpawnTimer.expired()) {
                addNewBarrier();
                barrierSpawnTimer.setTimer(std::rand() % nextBarrierSpawnRange[1] + nextBarrierSpawnRange[0]);

                barrierSpeedUpCounter++;
                if(barrierSpeedUpCounter >= barrierSpeedUpInterval) {
                    barrierSpeedUpCounter = 0;
                    barrierMovementInterval = std::max(minMovementInterval, barrierMovementInterval * speedUpFactor);
                    int next = std::min(13, barrierSpeedUpInterval + nextSpeedUpInterval);
                    barrierSpeedUpInterval = std::min(13, nextSpeedUpInterval);
                    nextSpeedUpInterval = next;
                }
            }
        }

        removeBarriers();
        return barriers;
    }

    void removeBarriers() {
        barriers.erase(std::remove_if(barriers.begin(), barriers.end(), [](const Barrier& b) { return b.xCoord <= 12; }), barriers.end());
    }

private:
    std::vector<Barrier> barriers;

    SimpleTimer barrierMovementTimer;
    float barrierMovementInterval = 80.0f;
    const float speedUpFactor = 0.75f;
    const float minMovementInterval = 5.0f;

    SimpleTimer barrierSpawnTimer;
    int nextBarrierSpawnRange[2] = {100, 350};

    int barrierSpeedUpInterval = 2;
    int nextSpeedUpInterval = 3;
    int barrierSpeedUpCounter = 0;

    const int minSpawnDistance = 40;
    const int spawnDistanceVariance = 40;

    bool frozen = false;
};