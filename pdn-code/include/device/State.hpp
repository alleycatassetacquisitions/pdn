#include <iostream>
#include <functional>
#include <vector>

// Forward declaration of State class
class State;

class StateTransition {
public:
    // Constructor
    StateTransition(std::function<bool()> condition, State* nextState)
        : condition(condition), nextState(nextState) {}

    // Method to check if the transition condition is met
    bool isConditionMet() const {
        return condition();
    }

    // Getter for the next state
    State* getNextState() const {
        return nextState;
    }

private:
    std::function<bool()> condition; // Function pointer that returns true based on the global state
    State* nextState;                // Pointer to the next state
};

// Example State class to demonstrate usage
class State {
public:
    State(const std::string& name) : name(name) {}

    void addTransition(StateTransition* transition) {
        transitions.push_back(transition);
    }

    void checkTransitions() const {
        for (const auto& transition : transitions) {
            if (transition->isConditionMet()) {
                std::cout << "Transitioning to state: " << transition->getNextState()->getName() << std::endl;
            }
        }
    }

    std::string getName() const {
        return name;
    }

private:
    std::string name;
    std::vector<StateTransition*> transitions;
};