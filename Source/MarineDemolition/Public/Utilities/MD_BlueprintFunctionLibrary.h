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
};
