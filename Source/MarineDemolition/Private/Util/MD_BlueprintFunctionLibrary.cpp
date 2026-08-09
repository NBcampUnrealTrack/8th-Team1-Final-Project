// Fill out your copyright notice in the Description page of Project Settings.


#include "Util/MD_BlueprintFunctionLibrary.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Types/Online/MD_SessionTypes.h"
#include "Types/SaveGame/MD_SaveDataTypes.h"

void UMD_BlueprintFunctionLibrary::CopyTextToClipboard(const FString& CopyString)
{
	// 시스템 Clipboard에 문자열 복사
	FPlatformApplicationMisc::ClipboardCopy(*CopyString);
}

FString UMD_BlueprintFunctionLibrary::PasteTextFromClipboard()
{
	// 시스템 Clipboard로 부터 복사된 문자열 얻기
	FString OutString;
	FPlatformApplicationMisc::ClipboardPaste(OutString);

	return OutString;
}

FMD_SessionCreateParams UMD_BlueprintFunctionLibrary::MakeMD_SessionCreateParams(TSoftObjectPtr<UWorld> SessionMap, bool bPrivateSession, bool bAllowJoinByFollow)
{
	return FMD_SessionCreateParams(SessionMap, bPrivateSession, bAllowJoinByFollow);
}

FMD_SessionFindParams UMD_BlueprintFunctionLibrary::MakeMD_SessionFindParams(FString ServerName, FString RoomCodeString)
{
	return FMD_SessionFindParams(ServerName, RoomCodeString);
}

FMD_GameData UMD_BlueprintFunctionLibrary::MakeMD_GameData(TSoftObjectPtr<UWorld> GameMap)
{
	FMD_GameData OutGameData = FMD_GameData();
	OutGameData.MapPackagePath = GameMap.ToSoftObjectPath().GetLongPackageName();
	return OutGameData;
}
