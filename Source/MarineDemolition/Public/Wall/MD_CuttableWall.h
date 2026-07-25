#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "GameFramework/Actor.h"
#include "MD_CuttableWall.generated.h"

class UMaterialInstanceDynamic;
class AMD_WallPiece;

// 격벽 타입. 칸막이 = 비내력벽(자유 철거), 지주 = 내력벽 (오절단 시 붕괴)
UENUM(BlueprintType)
enum class EMD_WallType : uint8
{
	Partition UMETA(DisplayName = "칸막이격벽"),
	Strut UMETA(DisplayName = "지주격벽"),
};

// 절단 격벽. 스캔 전에는 타입 구분 없는 외형, 전처리 게이트 통과 시만 절단 가능.
// 지주격벽을 지지대 없이 자르면 절단자가 있는 섹터의 천장이 붕괴한다. (시스템 기획 §2)
UCLASS()
class MARINEDEMOLITION_API AMD_CuttableWall : public AActor
{
	GENERATED_BODY()

public:
	AMD_CuttableWall();

	virtual void OnConstruction(const FTransform& Transform) override;

protected:
	virtual void BeginPlay() override;

public:

	// Cutter = 절단 버튼을 누른 캐릭터
	UFUNCTION(BlueprintCallable, Category = "Wall")
	void Cut(AActor* Cutter);

	UFUNCTION(BlueprintCallable, Category = "Wall")
	void AddPreprocess(float Amount);

	UFUNCTION(BlueprintPure, Category = "Wall")
	bool CanCut() const;
	
	UFUNCTION(BlueprintCallable, Category = "Wall")
	void ScanHighlight(float Duration = 3.0f);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Wall")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(EditAnywhere, Category = "Wall")
	EMD_WallType WallType = EMD_WallType::Partition;

	// 임시: 지지대 설치 여부. true면 오절단해도 붕괴하지 않음
	UPROPERTY(EditAnywhere, Category = "Wall")
	bool bHasSupport = false;

	// 절단 시 드랍할 조각 수 (기획: 5~10개)
	UPROPERTY(EditAnywhere, Category = "Wall")
	int32 PieceCount = 10;

	UPROPERTY(EditAnywhere, Category = "Wall")
	TSubclassOf<AMD_WallPiece> PieceClass;

	// 스캔 하이라이트 / 원복에 재사용할 다이내믹 머티리얼
	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MID;

	// 오염막 제거율 0 ~ 100
	UPROPERTY(EditAnywhere, Category="Preprocess", meta=(ClampMin = "0", ClampMax = "100"))
	float ContaminationRemoved = 0.0f;

	// 절단 허용 게이트. 회의 결정으로 기본 0(잠금 없음) — 정책이 바뀌면 에디터에서 수치만 올리면 됨
	UPROPERTY(EditAnywhere, Category="Preprocess", meta=(ClampMin = "0", ClampMax = "100"))
	float PartitionCutGate = 0.0f;

	UPROPERTY(EditAnywhere, Category="Preprocess", meta=(ClampMin = "0", ClampMax = "100"))
	float StrutCutGate = 0.0f;

	bool bCut = false;
	
	// 하이라이트 원복
	void RestoreScanColor();

	FTimerHandle ScanTimerHandle;
};
