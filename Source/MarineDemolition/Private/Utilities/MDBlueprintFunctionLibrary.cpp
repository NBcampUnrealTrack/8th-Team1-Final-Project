// Fill out your copyright notice in the Description page of Project Settings.


#include "Utilities/MDBlueprintFunctionLibrary.h"
#include "HAL/PlatformApplicationMisc.h"

void UMDBlueprintFunctionLibrary::CopyTextToClipboard(const FString& CopyString)
{
	// 시스템 Clipboard에 문자열 복사
	FPlatformApplicationMisc::ClipboardCopy(*CopyString);
}

FString UMDBlueprintFunctionLibrary::PasteTextFromClipboard()
{
	// 시스템 Clipboard로 부터 복사된 문자열 얻기
	FString OutString;
	FPlatformApplicationMisc::ClipboardPaste(OutString);

	return OutString;
}