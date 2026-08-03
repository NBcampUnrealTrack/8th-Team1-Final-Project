// Fill out your copyright notice in the Description page of Project Settings.


#include "Util/MD_BlueprintFunctionLibrary.h"
#include "HAL/PlatformApplicationMisc.h"

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
