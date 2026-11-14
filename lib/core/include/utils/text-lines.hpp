#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

/**
 * One pass of breakTextIntoLines(): breaks at spaces or fills every line;
 * `textWasCut` is set when visible text is left over.
 */
inline std::vector<std::string> fillTextLines(std::string remaining, int widthLimit,
                                              size_t lineLimit,
                                              const std::function<int(const char*)>& measureWidth,
                                              bool breakAtSpaces, bool& textWasCut) {
    std::vector<std::string> lines;
    while (lines.size() < lineLimit) {
        const size_t firstVisible = remaining.find_first_not_of(' ');
        if (firstVisible == std::string::npos) {
            break;
        }
        remaining.erase(0, firstVisible);

        // One character is taken even when it is too wide, so a limit narrower
        // than a glyph cannot stall the loop.
        size_t fitLength = 1;
        while (fitLength < remaining.size() &&
               measureWidth(remaining.substr(0, fitLength + 1).c_str()) <= widthLimit) {
            ++fitLength;
        }

        size_t lineLength = fitLength;
        const bool endsInsideWord = fitLength < remaining.size() && remaining[fitLength] != ' ';
        if (breakAtSpaces && endsInsideWord) {
            const size_t lastSpace = remaining.rfind(' ', fitLength - 1);
            if (lastSpace != std::string::npos) {
                lineLength = lastSpace;
            }
        }

        std::string line = remaining.substr(0, lineLength);
        line.erase(line.find_last_not_of(' ') + 1);
        lines.push_back(line);
        remaining.erase(0, lineLength);
    }
    textWasCut = remaining.find_first_not_of(' ') != std::string::npos;
    return lines;
}

/**
 * Breaks `text` at spaces into at most `lineLimit` lines no wider than
 * `widthLimit`; if text is left over, fills every line instead and cuts what
 * still does not fit. Blank text gives no lines.
 */
inline std::vector<std::string>
breakTextIntoLines(const std::string& text, int widthLimit, size_t lineLimit,
                   const std::function<int(const char*)>& measureWidth) {
    bool textWasCut = false;
    std::vector<std::string> lines =
    fillTextLines(text, widthLimit, lineLimit, measureWidth, true, textWasCut);
    if (textWasCut) {
        lines = fillTextLines(text, widthLimit, lineLimit, measureWidth, false, textWasCut);
    }
    return lines;
}
