#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MD_LoreCatalog.generated.h"

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

/** 현장에 배치되는 로어 기록물 한 건의 정적 정의 */
USTRUCT(BlueprintType)
struct MARINEDEMOLITION_API FMD_LoreDefinition
{
	GENERATED_BODY()

public:
	/** 수집 상태와 월드 액터를 연결하는 현장 내 고유 식별자 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Lore")
	FName LoreId;

	/** 발견 이후 코덱스 목록에 공개되는 기록물 이름 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Lore")
	FText EntryName;

	/** 해독 이후 코덱스 상세 화면에 공개되는 기록물 본문 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Lore", meta = (MultiLine = "true"))
	FText EntryContent;

	/** 미발견 항목에 선택적으로 제공하는 위치 또는 접근 조건 안내 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Lore", meta = (MultiLine = "true"))
	FText DiscoveryHint;

	/** 최초 수집 시 팀 크레딧 원장에 지급할 보상 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Lore|Reward", meta = (ClampMin = "0"))
	int32 RewardCredits = 30;

	/** 발견 직후 선행 기록 수집 여부를 검사해야 하는 암호화 기록 구분 */
	/** false : 발견 직후 선행 기록 수집 여부 검사 없이 EntryContent 읽기 가능 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Lore|Decryption")
	bool bEncryptedUntilPrerequisitesComplete = false;

	/** 암호화 해제 전에 모두 발견해야 하는 같은 카탈로그의 로어 식별자 목록 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MD|Lore|Decryption", meta = (EditCondition = "bEncryptedUntilPrerequisitesComplete", EditConditionHides))
	TArray<FName> DecryptionPrerequisiteLoreIds;

public:
	/** 월드 배치와 코덱스 등록에 사용할 최소 식별 정보의 유효성 확인 */
	bool IsValidDefinition() const;
};

/** 한 현장의 로어 목록과 코덱스 완성 보상을 관리하는 데이터 애셋 */
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_LoreCatalog : public UDataAsset
{
	GENERATED_BODY()

public:
	/** 저장 데이터와 현장 정의를 연결하는 프로젝트 전역 현장 식별자 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore")
	FName SiteId;

	/** 코덱스 상단에 표시하는 현장 이름 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore")
	FText SiteDisplayName;

	/** 배열 순서를 코덱스 표시 순서와 미스터리 전개 순서로 사용하는 기록물 목록 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore")
	TArray<FMD_LoreDefinition> LoreEntries;

	/** 전체 기록물 해독 완료 시 한 번 지급할 추가 보상 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MD|Lore|Reward", meta = (ClampMin = "0"))
	int32 CompletionRewardCredits = 0;

public:
	/** 로어 식별자로 정의를 찾아 블루프린트에 복사하는 조회 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	bool FindLore(FName LoreId, FMD_LoreDefinition& OutLore) const;

	/** 코덱스 진행률의 분모로 사용할 전체 기록물 개수 반환 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	int32 GetLoreCount() const;

	/** 발견 목록과 선행 조건으로 코덱스 공개 상태를 계산하는 함수 */
	UFUNCTION(BlueprintPure, Category = "MD|Lore")
	EMD_LoreEntryState ResolveLoreState(FName LoreId, const TArray<FName>& DiscoveredLoreIds) const;

	/** 중복 식별자와 잘못된 암호화 참조를 찾아 에셋 입력 오류를 보고하는 함수 */
	bool ValidateCatalog(TArray<FText>& OutErrors) const;

	/** 복사 없이 런타임 로직에서 사용할 로어 정의 포인터 조회 함수 */
	const FMD_LoreDefinition* FindLoreDefinition(FName LoreId) const;
};
