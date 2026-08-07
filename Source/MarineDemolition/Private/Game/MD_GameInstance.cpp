// Fill out your copyright notice in the Description page of Project Settings.


#include "Game/MD_GameInstance.h"

void UMD_GameInstance::Init()
{
	Super::Init();
}

void UMD_GameInstance::OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld)
{
	Super::OnWorldChanged(OldWorld, NewWorld);

    // 변경되는 맵의 유효성 확인
    if (IsValid(NewWorld) == false)
    {
        return;
    }

    // 맵 에셋의 패키지 경로 얻기
    FString NewMapPackageName = NewWorld->GetPackage()->GetName();

    // 맵 경로 저장
}
