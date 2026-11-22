#include "StartupToast.h"

#include "Language.h"
#include "Logger.h"
#include "Paths.h"
#include "Script.h"
#include "Settings.h"
#include "Utils.h"

#include <Windows.h>
#include <inc/natives.h>
#include <simpleini/SimpleIni.h>

#include <cstdint>
#include <string>

namespace {
enum class ToastState {
    WaitingForGameplay,
    Posted,
    Done,
};

constexpr const char* kFeedTextureDict = "CHAR_SOCIAL_CLUB";
constexpr const char* kFeedTextureName = "CHAR_SOCIAL_CLUB";
constexpr const char* kFeedSubject = "xmodbox.com";
constexpr int kRequiredReadyFrames = 5;
constexpr int kMaxTextureWaitFrames = 30;
constexpr uint64_t kDiagnosticDelayMs = 5000;
constexpr uint64_t kFeedDismissWindowMs = 8000;

ToastState gState = ToastState::WaitingForGameplay;
uint64_t gWaitStartedMs = 0;
int gReadyFrames = 0;
bool gTextureRequested = false;
bool gWaitDiagLogged = false;
int gFeedNotificationId = -1;
uint64_t gPostedAtMs = 0;

bool NativeBool(BOOL value) {
    return (static_cast<uint32_t>(value) & 0xFFu) != 0u;
}

std::string ReadMenuKeyLabel() {
    CSimpleIniA ini;
    ini.SetUnicode();
    const auto menuIniPath = Paths::DataDirectory() / L"settings_menu.ini";
    if (ini.LoadFile(menuIniPath.string().c_str()) < 0)
        return "F5";

    std::string key = Utils::Trim(ini.GetValue("MENU", "MenuKey", "F5"));
    if (key.empty())
        return "F5";
    if (key.size() > 4 && Utils::ToLower(key.substr(0, 4)) == "vk_f")
        key.erase(0, 3);
    return key;
}

bool IsGameplayReady() {
    const Ped ped = PLAYER::PLAYER_PED_ID();
    if (!NativeBool(ENTITY::DOES_ENTITY_EXIST(ped)))
        return false;
    if (NativeBool(DLC::GET_IS_LOADING_SCREEN_ACTIVE()))
        return false;
    if (NativeBool(CAM::IS_SCREEN_FADED_OUT()))
        return false;
    if (NativeBool(HUD::IS_PAUSE_MENU_ACTIVE()))
        return false;
    return true;
}

void LogWaitDiagnostics() {
    const Ped ped = PLAYER::PLAYER_PED_ID();
    const BOOL pedExists = ENTITY::DOES_ENTITY_EXIST(ped);
    const BOOL loading = DLC::GET_IS_LOADING_SCREEN_ACTIVE();
    const BOOL fadedIn = CAM::IS_SCREEN_FADED_IN();
    const BOOL pauseActive = HUD::IS_PAUSE_MENU_ACTIVE();
    const BOOL hudHidden = HUD::IS_HUD_HIDDEN();
    LOG_INFO("[StartupToast] Still waiting for gameplay: ped={}, exists=0x{:X}, "
             "loading=0x{:X}, fadedIn=0x{:X}, pause=0x{:X}, hudHidden=0x{:X}, readyFrames={}",
        ped, static_cast<uint32_t>(pedExists), static_cast<uint32_t>(loading),
        static_cast<uint32_t>(fadedIn), static_cast<uint32_t>(pauseActive),
        static_cast<uint32_t>(hudHidden), gReadyFrames);
}
}

void StartupToast::Reset() {
    gState = ToastState::WaitingForGameplay;
    gWaitStartedMs = GetTickCount64();
    gReadyFrames = 0;
    gTextureRequested = false;
    gWaitDiagLogged = false;
    gFeedNotificationId = -1;
    gPostedAtMs = 0;
}

void StartupToast::Dismiss() {
    if (gState == ToastState::Posted && gFeedNotificationId >= 0 &&
        GetTickCount64() - gPostedAtMs < kFeedDismissWindowMs) {
        HUD::THEFEED_REMOVE_ITEM(gFeedNotificationId);
    }
    gFeedNotificationId = -1;
    gState = ToastState::Done;
}

void StartupToast::Tick() {
    if (!gSettings.ShowStartupToast) {
        gState = ToastState::Done;
        return;
    }
    if (gState == ToastState::Done)
        return;
    if (gMenu.IsThisOpen()) {
        Dismiss();
        return;
    }

    const uint64_t now = GetTickCount64();
    if (gState == ToastState::Posted) {
        if (now - gPostedAtMs >= kFeedDismissWindowMs) {
            if (gTextureRequested) {
                GRAPHICS::SET_STREAMED_TEXTURE_DICT_AS_NO_LONGER_NEEDED(kFeedTextureDict);
                gTextureRequested = false;
            }
            gState = ToastState::Done;
        }
        return;
    }

    if (!gTextureRequested) {
        GRAPHICS::REQUEST_STREAMED_TEXTURE_DICT(kFeedTextureDict, FALSE);
        gTextureRequested = true;
    }

    if (!IsGameplayReady()) {
        gReadyFrames = 0;
        if (!gWaitDiagLogged && now - gWaitStartedMs >= kDiagnosticDelayMs) {
            gWaitDiagLogged = true;
            LogWaitDiagnostics();
        }
        return;
    }

    ++gReadyFrames;
    if (gReadyFrames < kRequiredReadyFrames)
        return;

    const bool txdLoaded = NativeBool(
        GRAPHICS::HAS_STREAMED_TEXTURE_DICT_LOADED(kFeedTextureDict));
    if (!txdLoaded && gReadyFrames < kMaxTextureWaitFrames)
        return;

    const std::string menuKey = ReadMenuKeyLabel();
    const std::string title = gLanguage.Text("toast.startup.title");
    const std::string body = gLanguage.Format("toast.startup.body", {
        { "key", "~y~" + menuKey + "~s~" },
    });
    if (title.empty() && body.empty()) {
        gState = ToastState::Done;
        return;
    }

    HUD::BEGIN_TEXT_COMMAND_THEFEED_POST("CELL_EMAIL_BCON");
    if (txdLoaded) {
        Utils::AddTextChunks(body);
        gFeedNotificationId = HUD::END_TEXT_COMMAND_THEFEED_POST_MESSAGETEXT(
            kFeedTextureDict, kFeedTextureName, FALSE, 0,
            title.c_str(), kFeedSubject);
    }
    else {
        if (!title.empty()) {
            Utils::AddTextChunks(title);
            if (!body.empty())
                HUD::ADD_TEXT_COMPONENT_SUBSTRING_PLAYER_NAME("~n~");
        }
        if (!body.empty())
            Utils::AddTextChunks(body);
        gFeedNotificationId = HUD::END_TEXT_COMMAND_THEFEED_POST_TICKER(FALSE, TRUE);
    }

    gPostedAtMs = now;
    gState = ToastState::Posted;
    LOG_INFO("[StartupToast] Posted TheFeed notification (id={}, key={}, txdLoaded={}, readyFrames={})",
        gFeedNotificationId, menuKey, txdLoaded, gReadyFrames);
}
