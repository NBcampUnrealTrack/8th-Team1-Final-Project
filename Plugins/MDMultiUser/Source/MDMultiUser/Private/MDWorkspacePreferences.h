#pragma once

#include "CoreMinimal.h"

/** Per-user, per-project preferences. Does not change Unreal's editor culture. */
class FMDWorkspacePreferences
{
public:
    static bool IsEnglish();
    static bool ShowDevices();
    static uint32 Revision();
    static bool Apply(bool bEnglish, bool bShowDevices);
    static FText Text(const TCHAR* Key, const TCHAR* Korean);
    static void OpenWindow();
    static void CloseWindow();
};

#define MDLOC(Key, Korean) FMDWorkspacePreferences::Text(TEXT(Key), TEXT(Korean))
