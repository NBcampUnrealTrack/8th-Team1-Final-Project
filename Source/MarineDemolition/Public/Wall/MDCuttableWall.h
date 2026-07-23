#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MDCuttableWall.generated.h"

// 임시 절단 벽 스텁. 격벽 철거 카드(전처리/스캐너 포함) 작업 시 대체 예정.
// 지주격벽을 지지대 없이 자르면 절단자가 있는 섹터의 천장이 붕괴한다. (시스템 기획 §2)
UCLASS()
class MARINEDEMOLITION_API AMDCuttableWall : public AActor
{
	GENERATED_BODY()

public:
	AMDCuttableWall();

	// Cutter = 절단 버튼을 누른 캐릭터
	UFUNCTION(BlueprintCallable, Category = "Wall")
	void Cut(AActor* Cutter);

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	UPROPERTY(VisibleAnywhere, Category = "Wall")
	TObjectPtr<UStaticMeshComponent> Mesh;

	// true = 지주격벽(위험), false = 칸막이격벽(자유 철거)
	UPROPERTY(EditAnywhere, Category = "Wall")
	bool bIsStrutWall = false;

	// 임시: 지지대 설치 여부. true면 오절단해도 붕괴하지 않음
	UPROPERTY(EditAnywhere, Category = "Wall")
	bool bHasSupport = false;

	bool bCut = false;
};
