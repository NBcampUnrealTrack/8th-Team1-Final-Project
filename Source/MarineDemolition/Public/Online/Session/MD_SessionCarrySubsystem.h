// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Types/Session/MD_PlayerCarryTypes.h"
#include "MD_SessionCarrySubsystem.generated.h"

class APlayerState;

/**
 * 맵 이동과 재접속 사이에 플레이어의 핫바와 상점 구매 상태를 메모리로 운반하는 서버 전용 보관소.
 * 파일 저장 없이 GameInstance 수명 동안만 유지된다.
 */
UCLASS()
class MARINEDEMOLITION_API UMD_SessionCarrySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// 트래블 직전과 로그아웃 시 현재 Pawn의 장비와 구매 이력을 개인 키로 보관
	bool CapturePlayer(const APlayerState* PlayerState);

	// 새 Pawn 생성 직후 보관된 기록을 장비와 구매 이력에 적용
	bool RestorePlayer(const APlayerState* PlayerState);

	// 보관된 모든 개인 기록 제거
	void Reset();

	// 온라인 식별자 우선, 오프라인 이름 대체의 개인 키
	static FString BuildPlayerKey(const APlayerState* PlayerState);

protected:
	// 개인 키별 운반 기록
	UPROPERTY(Transient)
	TMap<FString, FMD_PlayerData> PlayerFields;
};
