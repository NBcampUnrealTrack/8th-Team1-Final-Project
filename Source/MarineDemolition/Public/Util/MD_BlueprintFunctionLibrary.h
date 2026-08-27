// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MD_BlueprintFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class MARINEDEMOLITION_API UMD_BlueprintFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
	
public:
	// Clipboard에 임의 문자열을 복사하는 함수
	UFUNCTION(BlueprintCallable, Category = "System|Clipboard")
	static void CopyTextToClipboard(const FString& CopyString);

	// Clipboard로부터 복사된 문자열을 얻는 함수
	UFUNCTION(BlueprintCallable, Category = "System|Clipboard")
	static FString PasteTextFromClipboard();

	UFUNCTION(BlueprintPure, Category = "Online Session", meta = (NativeMakeFunc, DisplayName = "Make MD_Session Create Params"))
	static FMD_SessionCreateParams MakeMD_SessionCreateParams(TSoftObjectPtr<UWorld> SessionMap, bool bPrivateSession, bool bAllowJoinByFollow);

	UFUNCTION(BlueprintPure, Category = "Online Session", meta = (NativeMakeFunc, DisplayName = "Make MD_Session Find Params"))
	static FMD_SessionFindParams MakeMD_SessionFindParams(FString ServerName, FString RoomCodeString);

	UFUNCTION(BlueprintPure, Category = "Game Data", meta = (NativeMakeFunc, DisplayName = "Make MD_GameData"))
	static struct FMD_GameData MakeMD_GameData(TSoftObjectPtr<UWorld> GameMap);

	UFUNCTION(BlueprintPure, Category = "Utility", meta = (NativeMakeFunc, DisplayName = "Get Map Package Path"))
	static FString GetMapPackagePath(TSoftObjectPtr<UWorld> InMap);
};
