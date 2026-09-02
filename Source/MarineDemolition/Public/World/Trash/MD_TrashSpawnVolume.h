#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "Templates/SubclassOf.h"
#include "MD_TrashSpawnVolume.generated.h"

class AMD_TrashItem;
class UBoxComponent;
class USceneComponent;
class USplineComponent;

UENUM(BlueprintType)
enum class EMD_TrashSpawnShape : uint8
{
	Box UMETA(DisplayName = "Box"),
	Circle UMETA(DisplayName = "Circle"),
	CustomPolygon UMETA(DisplayName = "Custom Polygon")
};

/**
 * 기존 쓰레기 종류 테이블을 태그로 필터링해 선택한 영역 안에 무작위 배치한다.
 * 실제 생성은 서버에서만 수행하며, 생성된 AMD_TrashItem이 종류와 이동을 복제한다.
 */
UCLASS(Blueprintable)
class MARINEDEMOLITION_API AMD_TrashSpawnVolume : public AActor
{
	GENERATED_BODY()

public:
	AMD_TrashSpawnVolume();

	/** 현재 쓰레기를 유지한 채 SpawnCount개를 추가한다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Trash Spawn")
	int32 SpawnTrash();

	/** 이 볼륨이 생성했고 아직 살아 있는 쓰레기를 제거한다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Trash Spawn")
	void ClearSpawnedTrash();

	/** 기존 생성물을 제거한 뒤 SpawnCount개를 새로 배치한다. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category = "MD|Trash Spawn")
	int32 RegenerateTrash();

	UFUNCTION(BlueprintPure, Category = "MD|Trash Spawn")
	int32 GetSpawnedTrashCount() const;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn")
	TObjectPtr<USceneComponent> SceneRoot;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn")
	TObjectPtr<UBoxComponent> SpawnVolume;

	/** Circle 모드의 에디터 경계 미리보기. 실제 반지름은 CircleRadius가 결정한다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Shape")
	TObjectPtr<USplineComponent> CircleBoundary;

	/** Custom Polygon 모드에서 레벨 인스턴스의 스플라인 점을 직접 움직여 경계를 만든다. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Shape")
	TObjectPtr<USplineComponent> PolygonBoundary;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Shape")
	EMD_TrashSpawnShape SpawnShape = EMD_TrashSpawnShape::Box;

	/** Circle 모드의 로컬 반지름. 컴포넌트 스케일이 비균일하면 타원으로 확장할 수 있다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Shape",
		meta = (EditCondition = "SpawnShape == EMD_TrashSpawnShape::Circle",
			ClampMin = "1.0", UIMin = "1.0", Units = "cm"))
	float CircleRadius = 500.0f;

	/** Circle 및 Custom Polygon 영역의 중심 위·아래 높이. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Shape",
		meta = (EditCondition = "SpawnShape != EMD_TrashSpawnShape::Box",
			ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float ShapeHalfHeight = 100.0f;

	/** 곡선 경계를 다각형으로 근사할 때 세그먼트마다 사용할 샘플 수. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, AdvancedDisplay, Category = "MD|Trash Spawn|Shape",
		meta = (EditCondition = "SpawnShape == EMD_TrashSpawnShape::CustomPolygon",
			ClampMin = "1", ClampMax = "32", UIMin = "1", UIMax = "32"))
	int32 PolygonSamplesPerSegment = 8;

	/** 생성할 쓰레기 액터. 기본값은 AMD_TrashItem이다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn")
	TSubclassOf<AMD_TrashItem> TrashItemClass;

	/**
	 * 허용할 종류 카테고리 배열. 종류 행의 Category가 항목과 같거나 그 하위 태그면 허용된다.
	 * 예: Score.Sorting 하나는 전체, Score.Sorting.Metal 하나는 금속 종류만 허용한다.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn",
		meta = (Categories = "Score.Sorting"))
	TArray<FGameplayTag> AllowedTrashCategoryTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn",
		meta = (ClampMin = "0", UIMin = "0"))
	int32 SpawnCount = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn")
	bool bSpawnOnBeginPlay = true;

	/** 켜면 SpawnTrash를 호출할 때마다 새 시드를 만든다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Random")
	bool bRandomizeSeedEachSpawn = false;

	/** 같은 볼륨 설정에서 동일한 배치를 재현할 고정 시드. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Random",
		meta = (EditCondition = "!bRandomizeSeedEachSpawn"))
	int32 SpawnSeed = 1337;

	/** 생성된 쓰레기 중심 사이의 최소 거리. 0이면 간격 검사를 하지 않는다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Placement",
		meta = (ClampMin = "0.0", UIMin = "0.0", Units = "cm"))
	float MinimumSpacing = 75.0f;

	/** 한 개의 위치를 찾기 위해 다시 뽑을 최대 횟수. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Trash Spawn|Placement",
		meta = (ClampMin = "1", UIMin = "1"))
	int32 MaxPlacementAttemptsPerItem = 16;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "MD|Trash Spawn|Random")
	int32 LastUsedSeed = 0;

private:
	TArray<FName> BuildEligibleTrashKindIds() const;
	bool FindSpawnLocation(FRandomStream& RandomStream, const TArray<FVector>& OccupiedLocations,
		FVector& OutLocation) const;
	bool IsSpawnShapeConfigured() const;
	void UpdateShapeVisualization();
	void UpdateCircleBoundary();
	void CompactSpawnedTrashItems();

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMD_TrashItem>> SpawnedTrashItems;
};
