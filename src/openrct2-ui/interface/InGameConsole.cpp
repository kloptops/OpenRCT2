/*****************************************************************************
 * Copyright (c) 2014-2023 OpenRCT2 developers
 *
 * For a complete list of all authors, please refer to contributors.md
 * Interested in contributing? Visit https://github.com/OpenRCT2/OpenRCT2
 *
 * OpenRCT2 is licensed under the GNU General Public License version 3.
 *****************************************************************************/

#include "InGameConsole.h"

#include "Theme.h"

#include <algorithm>
#include <cstring>
#include <openrct2/Context.h>
#include <openrct2/Version.h>
#include <openrct2/config/Config.h>
#include <openrct2/drawing/Drawing.h>
#include <openrct2/interface/Colour.h>
#include <openrct2/interface/Window.h>
#include <openrct2/localisation/Language.h>
#include <openrct2/localisation/LocalisationService.h>

using namespace OpenRCT2::Ui;

static InGameConsole _inGameConsole;

static FontStyle InGameConsoleGetFontStyle()
{
    return (gConfigInterface.ConsoleSmallFont ? FontStyle::Small : FontStyle::Medium);
}

static int32_t InGameConsoleGetLineHeight()
{
    return FontGetLineHeight(InGameConsoleGetFontStyle());
}

InGameConsole::InGameConsole()
{
    InteractiveConsole::WriteLine(OPENRCT2_NAME " " OPENRCT2_VERSION);
    InteractiveConsole::WriteLine("Type 'help' for a list of available commands. Type 'hide' to hide the console.");
    InteractiveConsole::WriteLine("");
    WritePrompt();
}

void InGameConsole::WritePrompt()
{
    InteractiveConsole::WriteLine("> ");
}

void InGameConsole::Input(ConsoleInput input)
{
    switch (input)
    {
        case ConsoleInput::LineClear:
            ClearInput();
            RefreshCaret();
            break;
        case ConsoleInput::LineExecute:
            if (_consoleCurrentLine[0] != '\0')
            {
                HistoryAdd(_consoleCurrentLine);

                // Append text we are executing to prompt line
                _consoleLines.back().append(_consoleCurrentLine);

                Execute(_consoleCurrentLine);
                WritePrompt();
                ClearInput();
                RefreshCaret();
            }
            ScrollToEnd();
            break;
        case ConsoleInput::HistoryPrevious:
            if (_consoleHistoryIndex > 0)
            {
                _consoleHistoryIndex--;
                std::memcpy(_consoleCurrentLine, _consoleHistory[_consoleHistoryIndex], CONSOLE_INPUT_SIZE);
            }
            _consoleTextInputSession->Size = strlen(_consoleTextInputSession->Buffer);
            _consoleTextInputSession->Length = UTF8Length(_consoleTextInputSession->Buffer);
            _consoleTextInputSession->SelectionStart = strlen(_consoleCurrentLine);
            break;
        case ConsoleInput::HistoryNext:
            if (_consoleHistoryIndex < _consoleHistoryCount - 1)
            {
                _consoleHistoryIndex++;
                std::memcpy(_consoleCurrentLine, _consoleHistory[_consoleHistoryIndex], CONSOLE_INPUT_SIZE);
                _consoleTextInputSession->Size = strlen(_consoleTextInputSession->Buffer);
                _consoleTextInputSession->Length = UTF8Length(_consoleTextInputSession->Buffer);
                _consoleTextInputSession->SelectionStart = strlen(_consoleCurrentLine);
            }
            else
            {
                _consoleHistoryIndex = _consoleHistoryCount;
                ClearInput();
            }
            break;
        case ConsoleInput::ScrollPrevious:
        {
            int32_t scrollAmt = GetNumVisibleLines() - 1;
            Scroll(scrollAmt);
            break;
        }
        case ConsoleInput::ScrollNext:
        {
            int32_t scrollAmt = GetNumVisibleLines() - 1;
            Scroll(-scrollAmt);
            break;
        }
        default:
            break;
    }
}

void InGameConsole::ClearInput()
{
    _consoleCurrentLine[0] = 0;
    if (_isOpen)
    {
        ContextStartTextInput(_consoleCurrentLine, sizeof(_consoleCurrentLine));
    }
}

void InGameConsole::HistoryAdd(const utf8* src)
{
    if (_consoleHistoryCount >= CONSOLE_HISTORY_SIZE)
    {
        for (int32_t i = 0; i < _consoleHistoryCount - 1; i++)
            std::memcpy(_consoleHistory[i], _consoleHistory[i + 1], CONSOLE_INPUT_SIZE);
        _consoleHistoryCount--;
    }
    std::memcpy(_consoleHistory[_consoleHistoryCount++], src, CONSOLE_INPUT_SIZE);
    _consoleHistoryIndex = _consoleHistoryCount;
}

void InGameConsole::ScrollToEnd()
{
    const int32_t maxLines = GetNumVisibleLines();
    if (maxLines == 0)
        _consoleScrollPos = 0;
    else
        _consoleScrollPos = std::max<int32_t>(0, static_cast<int32_t>(_consoleLines.size()) - maxLines);
}

void InGameConsole::RefreshCaret(size_t position)
{
    _consoleCaretTicks = 0;
    _selectionStart = position;
    char tempString[TEXT_INPUT_SIZE] = { 0 };
    std::memcpy(tempString, &_consoleCurrentLine, _selectionStart);
    _caretScreenPosX = GfxGetStringWidthNoFormatting(tempString, InGameConsoleGetFontStyle());
}

void InGameConsole::Scroll(int32_t linesToScroll)
{
    const int32_t maxVisibleLines = GetNumVisibleLines();
    const int32_t numLines = static_cast<int32_t>(_consoleLines.size());
    if (numLines > maxVisibleLines)
    {
        int32_t maxScrollValue = numLines - maxVisibleLines;
        _consoleScrollPos = std::clamp<int32_t>(_consoleScrollPos - linesToScroll, 0, maxScrollValue);
    }
}

void InGameConsole::Clear()
{
    _consoleLines.clear();
    ScrollToEnd();
}

void InGameConsole::ClearLine()
{
    _consoleCurrentLine[0] = 0;
    RefreshCaret();
}

void InGameConsole::Open()
{
    _isOpen = true;
    ScrollToEnd();
    RefreshCaret();
    _consoleTextInputSession = ContextStartTextInput(_consoleCurrentLine, sizeof(_consoleCurrentLine));
}

void InGameConsole::Close()
{
    _consoleTextInputSession = nullptr;
    _isOpen = false;
    Invalidate();
    ContextStopTextInput();
}

void InGameConsole::Hide()
{
    Close();
}

void InGameConsole::Toggle()
{
    if (_isOpen)
    {
        Close();
    }
    else
    {
        Open();
    }
}

void InGameConsole::WriteLine(const std::string& input, FormatToken colourFormat)
{
    // Include text colour format only for special cases
    // The draw function handles the default text colour differently
    auto colourCodepoint = "";
    if (colourFormat != FormatToken::ColourWindow2)
        colourCodepoint = "{WINDOW_COLOUR_2}";

    std::string line;
    std::size_t splitPos = 0;
    std::size_t stringOffset = 0;
    while (splitPos != std::string::npos)
    {
        splitPos = input.find('\n', stringOffset);
        line = input.substr(stringOffset, splitPos - stringOffset);
        _consoleLines.emplace_back(colourCodepoint + line);
        stringOffset = splitPos + 1;
    }

    if (_consoleLines.size() > CONSOLE_MAX_LINES)
    {
        const std::size_t linesToErase = _consoleLines.size() - CONSOLE_MAX_LINES;
        _consoleLines.erase(_consoleLines.begin(), _consoleLines.begin() + linesToErase);
    }
}

void InGameConsole::Invalidate() const
{
    GfxSetDirtyBlocks({ _consoleTopLeft, _consoleBottomRight });
}

void InGameConsole::Update()
{
    _consoleTopLeft = { 0, 0 };
    _consoleBottomRight = { ContextGetWidth(), 322 };

    if (_isOpen)
    {
        // When scrolling the map, the console pixels get copied... therefore invalidate the screen
        WindowBase* mainWindow = WindowGetMain();
        if (mainWindow != nullptr)
        {
            Viewport* mainViewport = WindowGetViewport(mainWindow);
            if (mainViewport != nullptr)
            {
                if (_lastMainViewport != mainViewport->viewPos)
                {
                    _lastMainViewport = mainViewport->viewPos;

                    GfxInvalidateScreen();
                }
            }
        }
    }

    // Flash the caret
    _consoleCaretTicks = (_consoleCaretTicks + 1) % 30;
}

void InGameConsole::Draw(DrawPixelInfo* dpi) const
{
    if (!_isOpen)
        return;

    // Set font
    uint8_t textColour = NOT_TRANSLUCENT(ThemeGetColour(WindowClass::Console, 1));
    const int32_t lineHeight = InGameConsoleGetLineHeight();
    const int32_t maxLines = GetNumVisibleLines();

    // This is something of a hack to ensure the text is actually black
    // as opposed to a desaturated grey
    thread_local std::string _colourFormatStr;
    _colourFormatStr.clear();
    if (textColour == COLOUR_BLACK)
    {
        _colourFormatStr = "{BLACK}";
    }

    // TTF looks far better without the outlines
    if (!LocalisationService_UseTrueTypeFont())
    {
        textColour |= COLOUR_FLAG_OUTLINE;
    }

    Invalidate();

    // Give console area a translucent effect.
    GfxFilterRect(dpi, { _consoleTopLeft, _consoleBottomRight }, FilterPaletteID::Palette51);

    // Make input area more opaque.
    GfxFilterRect(
        dpi, { { _consoleTopLeft.x, _consoleBottomRight.y - lineHeight - 10 }, _consoleBottomRight - ScreenCoordsXY{ 0, 1 } },
        FilterPaletteID::Palette51);

    // Paint background colour.
    uint8_t backgroundColour = ThemeGetColour(WindowClass::Console, 0);
    GfxFillRectInset(dpi, { _consoleTopLeft, _consoleBottomRight }, backgroundColour, INSET_RECT_FLAG_FILL_NONE);
    GfxFillRectInset(
        dpi, { _consoleTopLeft + ScreenCoordsXY{ 1, 1 }, _consoleBottomRight - ScreenCoordsXY{ 1, 1 } }, backgroundColour,
        INSET_RECT_FLAG_BORDER_INSET);

    std::string lineBuffer;
    auto screenCoords = _consoleTopLeft + ScreenCoordsXY{ CONSOLE_EDGE_PADDING, CONSOLE_EDGE_PADDING };

    // Draw text inside console
    for (std::size_t i = 0; i < _consoleLines.size() && i < static_cast<size_t>(maxLines); i++)
    {
        const size_t index = i + _consoleScrollPos;
        lineBuffer = _colourFormatStr + _consoleLines[index];
        GfxDrawString(dpi, screenCoords, lineBuffer.c_str(), { textColour, InGameConsoleGetFontStyle() });
        screenCoords.y += lineHeight;
    }

    screenCoords.y = _consoleBottomRight.y - lineHeight - CONSOLE_EDGE_PADDING - 1;

    // Draw current line
    lineBuffer = _colourFormatStr + _consoleCurrentLine;
    DrawText(dpi, screenCoords, { TEXT_COLOUR_255, InGameConsoleGetFontStyle() }, lineBuffer.c_str(), true);

    // Draw caret
    if (_consoleCaretTicks < CONSOLE_CARET_FLASH_THRESHOLD)
    {
        auto caret = screenCoords + ScreenCoordsXY{ _caretScreenPosX, lineHeight };
        uint8_t caretColour = ColourMapA[BASE_COLOUR(textColour)].lightest;
        GfxFillRect(dpi, { caret, caret + ScreenCoordsXY{ CONSOLE_CARET_WIDTH, 1 } }, caretColour);
    }

    // What about border colours?
    uint8_t borderColour1 = ColourMapA[BASE_COLOUR(backgroundColour)].light;
    uint8_t borderColour2 = ColourMapA[BASE_COLOUR(backgroundColour)].mid_dark;

    // Input area top border
    GfxFillRect(
        dpi,
        { { _consoleTopLeft.x, _consoleBottomRight.y - lineHeight - 11 },
          { _consoleBottomRight.x, _consoleBottomRight.y - lineHeight - 11 } },
        borderColour1);
    GfxFillRect(
        dpi,
        { { _consoleTopLeft.x, _consoleBottomRight.y - lineHeight - 10 },
          { _consoleBottomRight.x, _consoleBottomRight.y - lineHeight - 10 } },
        borderColour2);

    // Input area bottom border
    GfxFillRect(
        dpi, { { _consoleTopLeft.x, _consoleBottomRight.y - 1 }, { _consoleBottomRight.x, _consoleBottomRight.y - 1 } },
        borderColour1);
    GfxFillRect(dpi, { { _consoleTopLeft.x, _consoleBottomRight.y }, _consoleBottomRight }, borderColour2);
}

// Calculates the amount of visible lines, based on the console size, excluding the input line.
int32_t InGameConsole::GetNumVisibleLines() const
{
    const int32_t lineHeight = InGameConsoleGetLineHeight();
    const int32_t consoleHeight = _consoleBottomRight.y - _consoleTopLeft.y;
    if (consoleHeight == 0)
        return 0;
    const int32_t drawableHeight = consoleHeight - 2 * lineHeight - 4; // input line, separator - padding
    return drawableHeight / lineHeight;
}
