#pragma once

#include <UUID.h>

class IdGenerator {
public:
    static IdGenerator *GetInstance() {
        static IdGenerator instance;
        return &instance;
    };

    char *generateId() {
        generator.generate();
        return generator.toCharArray();
    };

private:
    UUID generator;

    IdGenerator() {
        generator.setVariant4Mode();
        generator.seed(random(999999999), random(999999999));
    };
};
