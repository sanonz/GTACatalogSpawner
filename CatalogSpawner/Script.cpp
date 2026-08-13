#include "Script.h"

#include "ConfigLoader.h"
#include "ImageCache.h"
#include "Language.h"
#include "Logger.h"
#include "Paths.h"
#include "ScriptMenu.h"
#include "SceneLoader.h"
#include "Settings.h"

#include <inc/main.h>

NativeMenu::Menu gMenu;

void ReloadUserData() {
    gSettings.Load();
    gLanguage.Reload(Paths::DataDirectory() / L"Languages", gSettings.Language);
    if (gSettings.Language != gLanguage.ActiveCode()) {
        gSettings.Language = gLanguage.ActiveCode();
        gSettings.Save();
    }
    gCatalogs.Reload(Paths::DataDirectory());
}

void InitializeScript() {
    Paths::EnsureDataDirectories();
    gSettings.SetPath(Paths::DataDirectory() / L"settings.ini");
    gImages.Initialize(Paths::DataDirectory());
    ReloadUserData();

    gMenu.RegisterOnMain(OnMenuOpen);
    gMenu.RegisterOnExit(OnMenuExit);
    gMenu.SetFiles((Paths::DataDirectory() / L"settings_menu.ini").string());
    gMenu.ReadSettings();
    gMenu.Initialize();
    LOG_INFO("Initialization finished");
}

void ScriptMain() {
    InitializeScript();
    while (true) {
        SceneLoader::Tick();
        UpdateMenu();
        WAIT(0);
    }
}
