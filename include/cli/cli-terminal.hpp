#pragma once

#ifdef NATIVE_BUILD

// ============================================================================
// PLATFORM REQUIREMENT: This CLI tool requires a Unix/POSIX terminal.
// Windows is not currently supported due to differences in terminal handling.
// Use WSL, Git Bash, or a Linux/macOS system to run this tool.
// ============================================================================
#ifdef _WIN32
#error "The PDN CLI simulator requires a Unix/POSIX terminal. Please use WSL, Git Bash, or a Unix-based system."
#endif

#include <cstdio>
#include <string>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

namespace cli {

/**
 * Special key codes returned by readKey().
 * Regular ASCII characters are returned as-is (0-127).
 * Special keys use values >= 256.
 */
enum class Key : int {
    NONE = -1,
    ARROW_UP = 256,
    ARROW_DOWN = 257,
    ARROW_LEFT = 258,
    ARROW_RIGHT = 259,
};

/**
 * Terminal utilities for ANSI escape codes and platform-specific terminal handling.
 */
class Terminal {
public:
    /**
     * Clear the entire screen and move cursor to home position.
     */
    static void clearScreen() {
        printf("\033[2J\033[H");
        fflush(stdout);
    }

    /**
     * Hide the terminal cursor.
     */
    static void hideCursor() {
        printf("\033[?25l");
        fflush(stdout);
    }

    /**
     * Show the terminal cursor.
     */
    static void showCursor() {
        printf("\033[?25h");
        fflush(stdout);
    }

    /**
     * Move cursor to a specific row and column (1-indexed).
     */
    static void moveCursor(int row, int col) {
        printf("\033[%d;%dH", row, col);
        fflush(stdout);
    }

    /**
     * Clear the entire current line.
     */
    static void clearLine() {
        printf("\033[2K");
    }

    /**
     * Clear from cursor position to end of screen.
     */
    static void clearFromCursor() {
        printf("\033[J");
    }

    /**
     * Flush stdout.
     */
    static void flush() {
        fflush(stdout);
    }

    /**
     * Configure terminal for non-blocking, raw input.
     * Returns the old termios settings for restoration later.
     */
    static struct termios enableRawMode() {
        struct termios oldTermios, newTermios;
        tcgetattr(STDIN_FILENO, &oldTermios);
        newTermios = oldTermios;
        newTermios.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &newTermios);
        fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);
        return oldTermios;
    }

    /**
     * Restore terminal settings.
     */
    static void restoreTerminal(const struct termios& oldTermios) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldTermios);
    }

    /**
     * Read a single character from stdin (non-blocking).
     * Returns -1 if no character available, otherwise the character.
     */
    static int readChar() {
        char c;
        if (read(STDIN_FILENO, &c, 1) > 0) {
            return c;
        }
        return -1;
    }
    
    /**
     * Read a key from stdin, handling escape sequences for arrow keys.
     * Returns Key::NONE if no input available.
     * Returns Key::ARROW_UP, ARROW_DOWN, etc. for arrow keys.
     * Returns regular ASCII values (cast to int) for normal characters.
     */
    static int readKey() {
        int c = readChar();
        if (c == -1) return static_cast<int>(Key::NONE);
        
        // Check for escape sequence (arrow keys)
        if (c == 27) {  // ESC
            int c2 = readChar();
            if (c2 == '[') {
                int c3 = readChar();
                switch (c3) {
                    case 'A': return static_cast<int>(Key::ARROW_UP);
                    case 'B': return static_cast<int>(Key::ARROW_DOWN);
                    case 'C': return static_cast<int>(Key::ARROW_RIGHT);
                    case 'D': return static_cast<int>(Key::ARROW_LEFT);
                    default: break;
                }
            }
            // If not a recognized sequence, treat as plain ESC
            return 27;
        }
        
        return c;
    }
};

/**
 * Print the PDN ASCII art header.
 */
inline void printHeader() {
    printf("\033[1;36m"); // Bold cyan
    printf("╔══════════════════════════════════════════════════════════════════════════════╗\n");
    printf("║                                                                              ║\n");
    printf("║         ██████╗ ██████╗ ███╗   ██╗    ███████╗██╗███╗   ███╗                 ║\n");
    printf("║         ██╔══██╗██╔══██╗████╗  ██║    ██╔════╝██║████╗ ████║                 ║\n");
    printf("║         ██████╔╝██║  ██║██╔██╗ ██║    ███████╗██║██╔████╔██║                 ║\n");
    printf("║         ██╔═══╝ ██║  ██║██║╚██╗██║    ╚════██║██║██║╚██╔╝██║                 ║\n");
    printf("║         ██║     ██████╔╝██║ ╚████║    ███████║██║██║ ╚═╝ ██║                 ║\n");
    printf("║         ╚═╝     ╚═════╝ ╚═╝  ╚═══╝    ╚══════╝╚═╝╚═╝     ╚═╝                 ║\n");
    printf("║                                                                              ║\n");
    printf("║                          PDN Device Simulator                                ║\n");
    printf("║                                                                              ║\n");
    printf("╚══════════════════════════════════════════════════════════════════════════════╝\n");
    printf("\033[0m"); // Reset colors
    fflush(stdout);
}

/**
 * Helper to build a formatted string (like sprintf but returns std::string).
 */
template<typename... Args>
std::string format(const char* fmt, Args... args) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), fmt, args...);
    return std::string(buffer);
}

} // namespace cli

#endif // NATIVE_BUILD
