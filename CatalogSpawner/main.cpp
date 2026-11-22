#include "Logger.h"
#include "Paths.h"
#include "Script.h"
#include "TextInput.h"
#include "Version.h"

#include <Windows.h>
#include <inc/main.h>

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        Paths::SetModule(module);
        gLogger.SetPath(Paths::ModuleDirectory() / L"GTAVCatalogSpawner.log");
        gLogger.Clear();
        LOG_INFO("GTAV Catalog Spawner {}", CATALOG_SPAWNER_VERSION);
        scriptRegister(module, ScriptMain);
        keyboardHandlerRegister(TextInput::OnKeyboardMessage);
        break;
    case DLL_PROCESS_DETACH:
        keyboardHandlerUnregister(TextInput::OnKeyboardMessage);
        scriptUnregister(module);
        break;
    }
    return TRUE;
}

