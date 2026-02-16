#include "game/signal-echo/signal-echo-states.hpp"
#include "game/signal-echo/signal-echo.hpp"
#include "game/signal-echo/signal-echo-resources.hpp"
#include <cstdlib>

EchoLose::EchoLose(SignalEcho* game) : State(ECHO_LOSE) {
    this->game = game;
}

EchoLose::~EchoLose() {
    game = nullptr;
}

void EchoLose::onStateMounted(Device* PDN) {
    transitionToIntroState = false;
    phase = LosePhase::SCRAMBLING;
    scrambleFrameCount = 0;

    auto& session = game->getSession();

    MiniGameOutcome loseOutcome;
    loseOutcome.result = MiniGameResult::LOST;
    loseOutcome.score = session.score;
    loseOutcome.hardMode = (game->getConfig().sequenceLength >= 11 &&
                            game->getConfig().numSequences >= 5);
    game->setOutcome(loseOutcome);

    // Start scramble animation
    PDN->getHaptics()->max();

    // Red fade LED
    AnimationConfig config;
    config.type = AnimationType::IDLE;
    config.speed = 8;
    config.curve = EaseCurve::LINEAR;
    config.initialState = SIGNAL_ECHO_LOSE_STATE;
    config.loopDelayMs = 0;
    config.loop = true;
    PDN->getLightManager()->startAnimation(config);

    phaseTimer.setTimer(SCRAMBLE_FRAME_MS);
}

void EchoLose::onStateLoop(Device* PDN) {
    if (phase == LosePhase::SCRAMBLING) {
        if (!phaseTimer.expired()) {
            return;
        }

        // Render one frame of scramble
        auto& config = game->getConfig();
        int seqLen = config.sequenceLength;
        auto layout = SignalEchoRenderer::getLayout(seqLen);

        PDN->getDisplay()->invalidateScreen();
        SignalEchoRenderer::drawHUD(PDN->getDisplay(), game->getSession().currentRound,
                                     config.numSequences);
        SignalEchoRenderer::drawSeparators(PDN->getDisplay());

        // Random arrows in all slots
        for (int i = 0; i < layout.count; i++) {
            int x = layout.startX + i * (layout.slotWidth + layout.gap);
            int y = layout.startY;
            bool randomArrow = (rand() % 2) == 0;
            SignalEchoRenderer::drawSlotFilled(PDN->getDisplay(), x, y,
                                               layout.slotWidth, layout.slotHeight,
                                               randomArrow);
        }

        PDN->getDisplay()->render();

        scrambleFrameCount++;
        if (scrambleFrameCount * SCRAMBLE_FRAME_MS >= SCRAMBLE_DURATION_MS) {
            // Scramble complete — switch to message display
            PDN->getHaptics()->off();
            phase = LosePhase::MESSAGE_DISPLAY;
            phaseTimer.setTimer(MESSAGE_DISPLAY_MS);

            // Display "SIGNAL LOST"
            PDN->getDisplay()->invalidateScreen();
            PDN->getDisplay()->setGlyphMode(FontMode::TEXT)
                ->drawText("SIGNAL LOST", 20, 30);
            PDN->getDisplay()->render();
        } else {
            phaseTimer.setTimer(SCRAMBLE_FRAME_MS);
        }
    } else if (phase == LosePhase::MESSAGE_DISPLAY) {
        if (phaseTimer.expired()) {
            if (!game->getConfig().managedMode) {
                transitionToIntroState = true;
            } else {
                PDN->returnToPreviousApp();
            }
        }
    }
}

void EchoLose::onStateDismounted(Device* PDN) {
    phaseTimer.invalidate();
    transitionToIntroState = false;
    PDN->getHaptics()->off();
}

bool EchoLose::transitionToIntro() {
    return transitionToIntroState;
}

bool EchoLose::isTerminalState() const {
    return game->getConfig().managedMode;
}
