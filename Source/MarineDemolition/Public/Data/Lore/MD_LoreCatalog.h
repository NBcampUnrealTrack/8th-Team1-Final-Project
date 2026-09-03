#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_LoreCatalog.generated.h"

class UMD_LoreDefinition;

/** 코덱스 UI에서 사용하는 로어 기록물의 공개 상태 */
UENUM(BlueprintType)
enum class EMD_LoreEntryState : uint8
{
	/** 아직 조사되지 않아 이름과 내용을 공개하지 않는 상태 */
	Undiscovered UMETA(DisplayName = "미발견"),

	/** 기록물은 발견했지만 선행 기록이 부족해 내용을 공개하지 않는 상태 */
	Encrypted UMETA(DisplayName = "암호화됨"),

	/** 기록물의 전체 내용을 코덱스에서 열람할 수 있는 상태 */
	Decrypted UMETA(DisplayName = "해독됨")
};

/** 한 현장의 로어 정의 목록과 코덱스 표시 순서를 관리하는 데이터 애셋 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_LoreCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 코덱스 상단에 표시하는 현장 이름 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore")
	FText SiteDisplayName;

	/** 배열 순서를 코덱스 표시 순서와 미스터리 전개 순서로 사용하는 기록물 Asset ID 목록 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore", meta = (AllowedTypes = "Quest"))
	TArray<FPrimaryAssetId> LoreEntries;

public:
	/** 카탈로그 순서 인덱스로 등록된 기록물 정의 에셋을 조회하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	UMD_LoreDefinition* GetLoreDefinition(int32 LoreIndex) const;

	/** Primary Asset ID로 등록된 기록물 정의 에셋을 조회하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	UMD_LoreDefinition* FindLoreByAssetId(FPrimaryAssetId LoreAssetId) const;

	/** 코덱스 진행률의 분모로 사용할 전체 기록물 개수 반환 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	FORCEINLINE int32 GetLoreCount() const { return LoreEntries.Num(); }

	/** 발견 목록과 선행 조건으로 코덱스 공개 상태를 계산하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	EMD_LoreEntryState ResolveLoreState(FPrimaryAssetId LoreAssetId, const TArray<FPrimaryAssetId>& DiscoveredLoreAssetIds) const;

	/** 중복 식별자와 잘못된 암호화 참조를 찾아 에셋 입력 오류를 보고하는 함수 */
	bool ValidateCatalog(TArray<FText>& OutErrors) const;

};
