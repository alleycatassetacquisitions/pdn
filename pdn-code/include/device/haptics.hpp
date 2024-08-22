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
        Haptics(int pin);
        bool isOn();
        void max();
        void setIntensity(int intensity);
        void off();
        // void loadPattern(HapticsPattern pattern);
        // int playPattern();

    private:
        int pinNumber;
        int intensity;
        // HapticsPattern currentPattern;
        bool active = false;

};