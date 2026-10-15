// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "MD_ItemDefinition.generated.h"

class AActor;
class UTexture2D;
class UStaticMesh;
class UGameplayEffect;

// 정의에 저장하지 않고 지급 요청마다 전달할 대상 및 수량
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_ItemGrantContext
{
	GENERATED_BODY()

	// 아이템을 보관하거나 즉시 효과를 받을 액터
	UPROPERTY(BlueprintReadWrite, Category = "Grant")
	TObjectPtr<AActor> Recipient;

	// 상점 또는 지급 시스템이 확정한 이번 지급 수량
	UPROPERTY(BlueprintReadWrite, Category = "Grant")
	int32 Quantity = 1;

	// 서버 지급 호출부가 제공할 지속 업그레이드 및 구매 이력
	UPROPERTY(BlueprintReadWrite, Category = "Grant")
	TArray<FPrimaryAssetId> PurchasedItemIds;
};

// 모든 아이템의 식별 및 표시 정보와 지급 계약을 제공하는 공통 부모
UCLASS(Abstract, BlueprintType, Blueprintable)
class MARINEDEMOLITION_API UMD_ItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	virtual FPrimaryAssetId GetPrimaryAssetId() const override;

public:
	// 각 아이템 에셋에서 명시할 Primary Asset 분류
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FPrimaryAssetType AssetType;

	// 에셋 파일명과 독립적으로 유지할 아이템 식별자
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item", AssetRegistrySearchable)
	FName ItemId;

	// 아이템 공통 표시 이름
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	FText ItemName;

	// 아이템 공통 설명
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item", meta = (MultiLine = "true"))
	FText Description;

	// 상점과 Hotbar에서 사용할 공통 아이콘
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item")
	TObjectPtr<UTexture2D> ItemIcon;

	// Hotbar에서 선택한 아이템을 캐릭터 손에 표시할 기본 메시
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Visual")
	TObjectPtr<UStaticMesh> ToolMesh;

	/** Mass of one dropped item. Zero preserves the pickup actor's fallback mass. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Physics", meta = (ClampMin = "0.0", Units = "kg"))
	float WorldMassKg = 0.0f;

	// 아이템 메시를 부착할 소켓이며 미지정 시 캐릭터 기본 소켓 사용
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Visual")
	FName AttachSocketName;

	// 소켓 부착 후 메시별 손잡이 위치와 회전 및 크기를 보정할 트랜스폼
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Visual")
	FTransform AttachOffset = FTransform::Identity;

	// 구매 또는 장비 복구 시 소유자의 ASC에 적용하여 GA를 부여할 GE
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Item|Ability")
	TSubclassOf<UGameplayEffect> AbilityGrantEffect;

public:
	// 상태 변경 없이 지급 가능 여부를 확인할 확장 지점
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Grant")
	bool CanGrant(FMD_ItemGrantContext Context);
	// 파생 정의에서 재사용할 식별자 및 대상과 수량의 공통 검사
	virtual bool CanGrant_Implementation(FMD_ItemGrantContext Context);

	// 서버에서 실제 지급 결과를 반환할 확장 지점
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, BlueprintAuthorityOnly, Category = "Grant")
	bool Grant(FMD_ItemGrantContext Context);
	// 후속 지급 시스템이 연결되기 전 성공을 반환하지 않는 기본 처리
	virtual bool Grant_Implementation(FMD_ItemGrantContext Context);
	// 아이템 정의에 지정된 구매 시 능력 부여 GE 조회
	FORCEINLINE TSubclassOf<UGameplayEffect> GetAbilityGrantEffect() { return AbilityGrantEffect; }

	// 구매 GE가 부여한 능력 중 사용 입력에 연결할 태그 조회
	UFUNCTION(BlueprintPure, Category = "Item|Ability")
	virtual FGameplayTag GetUseAbilityTag() { return FGameplayTag(); }

};
