#include "TextInput.h"

#include "Language.h"
#include "SceneLoader.h"
#include "Script.h"
#include "Utils.h"

#include <GTAVMenuBase/menukeyboard.h>
#include <inc/enums.h>
#include <inc/main.h>
#include <inc/natives.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <format>
#include <mutex>
#include <vector>

namespace {
constexpr DWORD kKeyDrainTimeoutMs = 450;
constexpr ULONGLONG kCaretBlinkIntervalMs = 500;

enum class EventKind {
    Char,
    Backspace,
    Delete,
    Left,
    Right,
    Home,
    End,
    SelectAll,
    Paste,
    Copy,
    Cut,
    Confirm,
    Cancel,
};

struct KeyEvent {
    EventKind Kind = EventKind::Char;
    wchar_t Character = L'\0';
};

std::atomic<bool> gActive{ false };
std::mutex gQueueMutex;
std::vector<KeyEvent> gQueue;

void PushEvent(KeyEvent event) {
    std::lock_guard<std::mutex> lock(gQueueMutex);
    gQueue.push_back(event);
}

std::vector<KeyEvent> TakeEvents() {
    std::lock_guard<std::mutex> lock(gQueueMutex);
    std::vector<KeyEvent> events;
    events.swap(gQueue);
    return events;
}

void ClearEvents() {
    std::lock_guard<std::mutex> lock(gQueueMutex);
    gQueue.clear();
}

std::wstring ReadClipboardText() {
    if (!OpenClipboard(nullptr))
        return {};

    std::wstring result;
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* text = static_cast<const wchar_t*>(GlobalLock(handle))) {
            result = text;
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return result;
}

void WriteClipboardText(const std::wstring& text) {
    if (!OpenClipboard(nullptr))
        return;

    EmptyClipboard();
    const size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* dest = GlobalLock(memory)) {
            std::memcpy(dest, text.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory))
                GlobalFree(memory);
        }
        else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

std::wstring SanitizeSingleLine(const std::wstring& raw) {
    std::wstring cleaned;
    cleaned.reserve(raw.size());
    bool lastWasSpace = false;
    for (const wchar_t ch : raw) {
        if (ch == L'\r' || ch == L'\n' || ch == L'\t') {
            if (!cleaned.empty() && !lastWasSpace) {
                cleaned.push_back(L' ');
                lastWasSpace = true;
            }
            continue;
        }
        if (ch < 0x20 || ch == 0x7F)
            continue;
        cleaned.push_back(ch);
        lastWasSpace = (ch == L' ');
    }
    return cleaned;
}

void DisableGameControlsThisFrame() {
    PAD::DISABLE_ALL_CONTROL_ACTIONS(0);
    PAD::DISABLE_ALL_CONTROL_ACTIONS(2);
    PAD::DISABLE_CONTROL_ACTION(0, ControlFrontendPause, true);
    PAD::DISABLE_CONTROL_ACTION(0, ControlFrontendPauseAlternate, true);
    PAD::DISABLE_CONTROL_ACTION(2, ControlFrontendPause, true);
    PAD::DISABLE_CONTROL_ACTION(2, ControlFrontendPauseAlternate, true);
    HUD::HIDE_HELP_TEXT_THIS_FRAME();
}

bool AnyCloseKeyDown() {
    const auto& controls = gMenu.GetControls();
    const int selectKey = controls.ControlKeys[NativeMenu::MenuControls::MenuSelect];
    const int cancelKey = controls.ControlKeys[NativeMenu::MenuControls::MenuCancel];
    if (NativeMenu::IsKeyDown(VK_RETURN) || NativeMenu::IsKeyDown(VK_ESCAPE) ||
        NativeMenu::IsKeyDown(VK_BACK) || NativeMenu::IsKeyDown(VK_DELETE)) {
        return true;
    }
    if (selectKey > 0 && NativeMenu::IsKeyDown(static_cast<DWORD>(selectKey)))
        return true;
    if (cancelKey > 0 && NativeMenu::IsKeyDown(static_cast<DWORD>(cancelKey)))
        return true;
    if (PAD::IS_DISABLED_CONTROL_PRESSED(0, ControlFrontendAccept) ||
        PAD::IS_DISABLED_CONTROL_PRESSED(0, ControlFrontendCancel) ||
        PAD::IS_DISABLED_CONTROL_PRESSED(0, ControlFrontendPause) ||
        PAD::IS_DISABLED_CONTROL_PRESSED(0, ControlFrontendPauseAlternate)) {
        return true;
    }
    return false;
}

void DrainCloseKeys() {
    const ULONGLONG started = GetTickCount64();
    while (AnyCloseKeyDown() && GetTickCount64() - started < kKeyDrainTimeoutMs) {
        DisableGameControlsThisFrame();
        SceneLoader::Tick();
        WAIT(0);
    }
    for (int frame = 0; frame < 3; ++frame) {
        DisableGameControlsThisFrame();
        SceneLoader::Tick();
        WAIT(0);
    }
}

float EffectiveScale(float baseScale, int font) {
    return font == 0 ? baseScale * 0.75f : baseScale;
}

float MeasureTextWidth(const std::string& utf8, float baseScale, int font) {
    if (utf8.empty())
        return 0.0f;
    const float scale = EffectiveScale(baseScale, font);
    HUD::BEGIN_TEXT_COMMAND_GET_SCREEN_WIDTH_OF_DISPLAY_TEXT("CELL_EMAIL_BCON");
    Utils::AddTextChunks(utf8);
    HUD::SET_TEXT_FONT(font);
    HUD::SET_TEXT_SCALE(scale, scale);
    return HUD::END_TEXT_COMMAND_GET_SCREEN_WIDTH_OF_DISPLAY_TEXT(TRUE);
}

void DrawTextSingleLine(const std::string& utf8, int font, float x, float y,
    float baseScale, int r, int g, int b, int a, int justify = 1,
    float wrapStart = 0.0f, float wrapEnd = 1.0f) {
    if (utf8.empty())
        return;
    const float scale = EffectiveScale(baseScale, font);
    const float adjustedY = font == 0 ? y + 0.0025f : y;
    HUD::SET_TEXT_FONT(font);
    HUD::SET_TEXT_SCALE(0.0f, scale);
    HUD::SET_TEXT_COLOUR(r, g, b, a);
    HUD::SET_TEXT_JUSTIFICATION(justify);
    HUD::SET_TEXT_WRAP(wrapStart, wrapEnd);
    HUD::BEGIN_TEXT_COMMAND_DISPLAY_TEXT("CELL_EMAIL_BCON");
    Utils::AddTextChunks(utf8);
    HUD::END_TEXT_COMMAND_DISPLAY_TEXT({ x, adjustedY }, 0);
}

std::string SliceUtf8(const std::wstring& buffer, size_t start, size_t count) {
    if (start >= buffer.size() || count == 0)
        return {};
    const size_t safeCount = std::min(count, buffer.size() - start);
    return Utils::WideToUtf8(buffer.data() + start, static_cast<unsigned int>(safeCount));
}

void DrawInputDialog(const std::string& title, const std::wstring& buffer,
    size_t cursor, size_t& viewStart, bool allSelected, int maxLength,
    ULONGLONG lastActivityMs) {
    const float aspectRatio = std::max(0.5f, GRAPHICS::GET_ASPECT_RATIO(FALSE));
    const float aspectScale = (16.0f / 9.0f) / aspectRatio;
    const float boxWidth = std::clamp(0.38f * aspectScale, 0.28f, 0.52f);
    constexpr float centerX = 0.5f;
    const float leftX = centerX - boxWidth * 0.5f;
    const float rightX = centerX + boxWidth * 0.5f;

    constexpr float topY = 0.37f;
    constexpr float headerH = 0.036f;
    constexpr float bodyH = 0.054f;
    constexpr float footerH = 0.028f;
    constexpr float fieldPadX = 0.009f;
    constexpr float fieldH = 0.034f;
    constexpr float textPadX = 0.006f;

    const int font = gMenu.optionsFont;
    constexpr float headerTextScale = 0.42f;
    constexpr float inputTextScale = 0.44f;
    constexpr float footerTextScale = 0.34f;

    GRAPHICS::RESET_SCRIPT_GFX_ALIGN();

    // Subtle screen dim behind the single-line input card.
    GRAPHICS::DRAW_RECT({ 0.5f, 0.5f }, 1.0f, 1.0f, 0, 0, 0, 95, 0);

    // Header bar matching the menu theme color.
    const float headerCenterY = topY + headerH * 0.5f;
    GRAPHICS::DRAW_RECT({ centerX, headerCenterY }, boxWidth, headerH,
        gMenu.titleBackgroundColor.R, gMenu.titleBackgroundColor.G,
        gMenu.titleBackgroundColor.B, 245, 0);

    DrawTextSingleLine(title, font, leftX + fieldPadX, topY + 0.004f,
        headerTextScale, gMenu.titleTextColor.R, gMenu.titleTextColor.G,
        gMenu.titleTextColor.B, 255, 1, leftX + fieldPadX, rightX - 0.055f);

    const std::string counter = std::format("{} / {}", buffer.size(), maxLength);
    DrawTextSingleLine(counter, font, leftX, topY + 0.004f,
        headerTextScale, gMenu.titleTextColor.R, gMenu.titleTextColor.G,
        gMenu.titleTextColor.B, 230, 2, leftX, rightX - fieldPadX);

    // Single-line input body.
    const float bodyTopY = topY + headerH;
    const float bodyCenterY = bodyTopY + bodyH * 0.5f;
    GRAPHICS::DRAW_RECT({ centerX, bodyCenterY }, boxWidth, bodyH, 14, 16, 22, 238, 0);

    // Input border + dark input field.
    const float fieldWidth = boxWidth - fieldPadX * 2.0f;
    const float borderX = 0.0016f * aspectScale;
    constexpr float borderY = 0.0026f;
    GRAPHICS::DRAW_RECT({ centerX, bodyCenterY }, fieldWidth + borderX * 2.0f,
        fieldH + borderY * 2.0f, 95, 165, 245, 255, 0);
    GRAPHICS::DRAW_RECT({ centerX, bodyCenterY }, fieldWidth, fieldH,
        24, 27, 34, 255, 0);

    // Compute horizontal single-line scroll window so caret stays visible.
    const float textLeftX = (centerX - fieldWidth * 0.5f) + textPadX;
    const float maxTextWidth = std::max(0.05f, fieldWidth - textPadX * 2.0f);

    if (cursor < viewStart)
        viewStart = cursor;
    if (viewStart > buffer.size())
        viewStart = buffer.size();

    while (viewStart < cursor &&
        MeasureTextWidth(SliceUtf8(buffer, viewStart, cursor - viewStart),
            inputTextScale, font) > maxTextWidth - 0.004f) {
        ++viewStart;
    }

    size_t viewEnd = buffer.size();
    while (viewEnd > cursor &&
        MeasureTextWidth(SliceUtf8(buffer, viewStart, viewEnd - viewStart),
            inputTextScale, font) > maxTextWidth) {
        --viewEnd;
    }

    const std::string visibleText = SliceUtf8(buffer, viewStart, viewEnd - viewStart);
    const std::string prefixText = SliceUtf8(buffer, viewStart, cursor - viewStart);
    const float visibleWidth = MeasureTextWidth(visibleText, inputTextScale, font);
    const float prefixWidth = MeasureTextWidth(prefixText, inputTextScale, font);

    // Selection highlight when all text is selected (e.g. initial value or Ctrl+A).
    if (allSelected && !visibleText.empty()) {
        const float highlightWidth = std::min(visibleWidth + 0.003f, maxTextWidth);
        GRAPHICS::DRAW_RECT({ textLeftX + highlightWidth * 0.5f, bodyCenterY },
            highlightWidth, fieldH - 0.008f,
            gMenu.titleBackgroundColor.R, gMenu.titleBackgroundColor.G,
            gMenu.titleBackgroundColor.B, 185, 0);
    }

    const float textTopY = bodyCenterY - fieldH * 0.5f + 0.003f;
    DrawTextSingleLine(visibleText, font, textLeftX, textTopY,
        inputTextScale, 255, 255, 255, 255, 1, textLeftX, textLeftX + maxTextWidth + 0.05f);

    // Blinking vertical caret when not all-selected.
    const bool caretVisible = ((GetTickCount64() - lastActivityMs) / kCaretBlinkIntervalMs) % 2 == 0;
    if (!allSelected && caretVisible) {
        const float caretX = std::min(textLeftX + prefixWidth + 0.001f, textLeftX + maxTextWidth);
        GRAPHICS::DRAW_RECT({ caretX, bodyCenterY }, 0.0016f * aspectScale,
            fieldH - 0.010f, 255, 255, 255, 255, 0);
    }

    // Hint footer bar.
    const float footerTopY = bodyTopY + bodyH;
    const float footerCenterY = footerTopY + footerH * 0.5f;
    GRAPHICS::DRAW_RECT({ centerX, footerCenterY }, boxWidth, footerH, 0, 0, 0, 220, 0);
    DrawTextSingleLine(gLanguage.Text("input.hint"), font, leftX + fieldPadX,
        footerTopY + 0.003f, footerTextScale, 200, 205, 215, 255, 1,
        leftX + fieldPadX, rightX - fieldPadX);
}
}

void TextInput::OnKeyboardMessage(DWORD key, WORD /*repeats*/, BYTE scanCode,
    BOOL /*isExtended*/, BOOL isWithAlt, BOOL wasDownBefore, BOOL isUpNow) {
    if (!gActive.load(std::memory_order_acquire) || isUpNow)
        return;

    const bool ctrlDown = ((GetKeyState(VK_CONTROL) | GetAsyncKeyState(VK_CONTROL)) & 0x8000) != 0;
    const bool altDown = isWithAlt ||
        (((GetKeyState(VK_MENU) | GetAsyncKeyState(VK_MENU)) & 0x8000) != 0);

    if (ctrlDown && !altDown) {
        if (wasDownBefore)
            return;
        if (key == 'V') {
            PushEvent({ EventKind::Paste });
        }
        else if (key == 'C') {
            PushEvent({ EventKind::Copy });
        }
        else if (key == 'X') {
            PushEvent({ EventKind::Cut });
        }
        else if (key == 'A') {
            PushEvent({ EventKind::SelectAll });
        }
        return;
    }

    switch (key) {
    case VK_RETURN:
        if (!wasDownBefore)
            PushEvent({ EventKind::Confirm });
        return;
    case VK_ESCAPE:
        if (!wasDownBefore)
            PushEvent({ EventKind::Cancel });
        return;
    case VK_BACK:
        PushEvent({ EventKind::Backspace });
        return;
    case VK_DELETE:
        PushEvent({ EventKind::Delete });
        return;
    case VK_LEFT:
        PushEvent({ EventKind::Left });
        return;
    case VK_RIGHT:
        PushEvent({ EventKind::Right });
        return;
    case VK_HOME:
        PushEvent({ EventKind::Home });
        return;
    case VK_END:
        PushEvent({ EventKind::End });
        return;
    default:
        break;
    }

    BYTE keyboardState[256]{};
    GetKeyboardState(keyboardState);
    const auto syncKeyState = [&](int vk) {
        const SHORT sync = GetKeyState(vk);
        const SHORT async = GetAsyncKeyState(vk);
        BYTE state = 0;
        if (((sync | async) & 0x8000) != 0)
            state |= 0x80;
        if ((sync & 0x0001) != 0)
            state |= 0x01;
        keyboardState[vk] = state;
    };
    syncKeyState(VK_SHIFT);
    syncKeyState(VK_LSHIFT);
    syncKeyState(VK_RSHIFT);
    syncKeyState(VK_CAPITAL);
    syncKeyState(VK_NUMLOCK);
    keyboardState[VK_CONTROL] = 0;
    keyboardState[VK_LCONTROL] = 0;
    keyboardState[VK_RCONTROL] = 0;

    wchar_t chars[8]{};
    const int count = ToUnicodeEx(key, scanCode, keyboardState, chars, 8, 0, GetKeyboardLayout(0));
    if (count > 0) {
        for (int index = 0; index < count; ++index) {
            const wchar_t ch = chars[index];
            if (ch >= 0x20 && ch != 0x7F)
                PushEvent({ EventKind::Char, ch });
        }
    }
}

std::optional<std::string> TextInput::Prompt(const std::string& title,
    const std::string& defaultText, int maxLength) {
    const int clampedMax = std::clamp(maxLength, 1, 128);
    std::wstring buffer = SanitizeSingleLine(Utils::Utf8ToWide(defaultText));
    if (static_cast<int>(buffer.size()) > clampedMax)
        buffer.resize(static_cast<size_t>(clampedMax));

    size_t cursor = buffer.size();
    size_t viewStart = 0;
    bool allSelected = !buffer.empty();
    ULONGLONG lastActivityMs = GetTickCount64();

    const std::string displayTitle = title.empty() ? gLanguage.Text("input.default_title") : title;

    ClearEvents();
    gActive.store(true, std::memory_order_release);

    bool finished = false;
    bool confirmed = false;

    while (!finished) {
        DisableGameControlsThisFrame();

        const auto events = TakeEvents();
        for (const auto& event : events) {
            lastActivityMs = GetTickCount64();
            switch (event.Kind) {
            case EventKind::Confirm:
                confirmed = true;
                finished = true;
                break;
            case EventKind::Cancel:
                confirmed = false;
                finished = true;
                break;
            case EventKind::SelectAll:
                allSelected = !buffer.empty();
                cursor = buffer.size();
                break;
            case EventKind::Copy:
                if (!buffer.empty())
                    WriteClipboardText(buffer);
                break;
            case EventKind::Cut:
                if (!buffer.empty()) {
                    WriteClipboardText(buffer);
                    buffer.clear();
                    cursor = 0;
                    viewStart = 0;
                    allSelected = false;
                }
                break;
            case EventKind::Paste: {
                std::wstring pasted = SanitizeSingleLine(ReadClipboardText());
                if (pasted.empty())
                    break;
                if (allSelected) {
                    buffer.clear();
                    cursor = 0;
                    viewStart = 0;
                    allSelected = false;
                }
                const size_t remaining = static_cast<size_t>(clampedMax) -
                    std::min(buffer.size(), static_cast<size_t>(clampedMax));
                if (remaining > 0) {
                    if (pasted.size() > remaining)
                        pasted.resize(remaining);
                    buffer.insert(cursor, pasted);
                    cursor += pasted.size();
                }
                break;
            }
            case EventKind::Backspace:
                if (allSelected) {
                    buffer.clear();
                    cursor = 0;
                    viewStart = 0;
                    allSelected = false;
                }
                else if (cursor > 0) {
                    buffer.erase(cursor - 1, 1);
                    --cursor;
                }
                break;
            case EventKind::Delete:
                if (allSelected) {
                    buffer.clear();
                    cursor = 0;
                    viewStart = 0;
                    allSelected = false;
                }
                else if (cursor < buffer.size()) {
                    buffer.erase(cursor, 1);
                }
                break;
            case EventKind::Left:
                if (allSelected) {
                    allSelected = false;
                    cursor = 0;
                }
                else if (cursor > 0) {
                    --cursor;
                }
                break;
            case EventKind::Right:
                if (allSelected) {
                    allSelected = false;
                    cursor = buffer.size();
                }
                else if (cursor < buffer.size()) {
                    ++cursor;
                }
                break;
            case EventKind::Home:
                allSelected = false;
                cursor = 0;
                break;
            case EventKind::End:
                allSelected = false;
                cursor = buffer.size();
                break;
            case EventKind::Char:
                if (allSelected) {
                    buffer.clear();
                    cursor = 0;
                    viewStart = 0;
                    allSelected = false;
                }
                if (static_cast<int>(buffer.size()) < clampedMax) {
                    buffer.insert(cursor, 1, event.Character);
                    ++cursor;
                }
                break;
            }
            if (finished)
                break;
        }

        if (!finished) {
            DrawInputDialog(displayTitle, buffer, cursor, viewStart,
                allSelected, clampedMax, lastActivityMs);
            SceneLoader::Tick();
            WAIT(0);
        }
    }

    gActive.store(false, std::memory_order_release);
    ClearEvents();
    DrainCloseKeys();

    if (!confirmed)
        return std::nullopt;
    return Utils::WideToUtf8(buffer.data(), static_cast<unsigned int>(buffer.size()));
}
