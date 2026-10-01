// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Gameplay/Interaction/MD_Interactable.h"
#include "GameFramework/Actor.h"
#include "MD_SectorCloseoutMarker.generated.h"

class AMD_Sector;

// 섹터 구역 마감 제출 마커. 제출 시 평가 +40, 마감 상태는 Day 간 유지 (기획 02 §3)
UCLASS()
class MARINEDEMOLITION_API AMD_SectorCloseoutMarker : public AActor, public IMD_Interactable
{
	GENERATED_BODY()

public:
	AMD_SectorCloseoutMarker();

	virtual bool RequiresServerInteraction_Implementation() const override { return false; }
	virtual void Interact_Implementation(AActor* Interactor) override;
	virtual void SetInteractionHighlighted_Implementation(bool bHighlighted) override;

	// 서버 전용 마감 제출. 이미 마감이면 무시
	void TryCloseout();

	// 점수 재지급 없이 마감 여부를 양방향으로 복원하며 기존 호출은 마감 상태 유지
	void RestoreClosed(bool bWasClosed = true);

	bool IsClosed() const { return bClosed; }

protected:
	// 마감 시 비주얼 훅 (마커 색 변경 등)
	UFUNCTION(BlueprintImplementableEvent, Category = "Closeout")
	void OnCloseoutStateChanged(bool bNewClosed);

	UPROPERTY(VisibleAnywhere, Category = "Closeout")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// 마감 대상 섹터. 표시·후속 조건 검사용 (현재 점수 지급에는 미사용)
	UPROPERTY(EditAnywhere, Category = "Closeout")
	TObjectPtr<AMD_Sector> TargetSector;

	UPROPERTY(ReplicatedUsing = OnRep_Closed)
	bool bClosed = false;

	UFUNCTION()
	void OnRep_Closed();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
