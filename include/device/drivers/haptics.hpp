#pragma once


// class HapticsKeyFrame {
//     public:
//         int intensity;
//         int duration;
// };

// class HapticsPattern {

//     public:
//         HapticsPattern(
//             HapticsKeyFrame pattern[]
//         );

//         HapticsPattern();

//     protected:
//         HapticsKeyFrame getNextKeyFrame();
//         int getSize();
//         bool lastKeyFrame();
//         int totalDuration();

//     private:
//         int duration;
//         int currentKeyFrame;
//         int size;
//         HapticsKeyFrame* pattern;

//         int calculateDuration();
// };


class Haptics {
public:
    virtual ~Haptics() = default;

    virtual bool isOn() = 0;

    virtual void max() = 0;

    virtual void setIntensity(int intensity) = 0;

    virtual int getIntensity() = 0;

    virtual void off() = 0;

    // void loadPattern(HapticsPattern pattern);
    // int playPattern();
};
