#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Types/Customization/MD_CharacterAppearanceTypes.h"
#include "MD_CharacterRosterData.generated.h"

class UAnimInstance;
class UMaterialInterface;
class USkeletalMesh;
class UTexture2D;

// Palette의 ColorIndex에 대응하는 머티리얼 세트. 슬롯 순서대로 지정한다.
USTRUCT(BlueprintType)
struct FMD_CharacterMaterialVariant
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materials")
	TArray<TSoftObjectPtr<UMaterialInterface>> BodyMaterials;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Materials")
	TArray<TSoftObjectPtr<UMaterialInterface>> ArmsMaterials;
};

// 선택 가능한 캐릭터 한 명. 항목을 추가하면 선택 화면 버튼도 함께 늘어난다.
USTRUCT(BlueprintType)
struct FMD_CharacterRosterEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	FText DisplayName;

	// 비워 두면 버튼에 이름 텍스트를 대신 표시한다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	TSoftObjectPtr<UTexture2D> Portrait;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	TSoftObjectPtr<USkeletalMesh> BodyMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	TSoftClassPtr<UAnimInstance> BodyAnimClass;

	// 1인칭 팔은 몸 메시의 포즈를 따라가므로(LeaderPose) 본 이름이 몸과 같아야 한다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	TSoftObjectPtr<USkeletalMesh> ArmsMesh;

	// 팔 메시 슬롯 순서대로 덮어쓸 머티리얼. 비우면 팔 메시 기본 머티리얼을 쓴다
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	TArray<TSoftObjectPtr<UMaterialInterface>> ArmsMaterials;

	// Palette와 같은 순서. 0은 원본. 항목/슬롯이 없으면 원본으로 복원한다.
	// 비어 있으면 기존 벡터 파라미터 방식으로 색상을 적용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Character")
	TArray<FMD_CharacterMaterialVariant> MaterialVariants;
};

// 팔레트 색 하나
USTRUCT(BlueprintType)
struct FMD_CharacterPaletteEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Palette")
	FLinearColor Color = FLinearColor::White;
};

// 캐릭터 목록과 팔레트의 정본. Project Settings → Game → MD Character Appearance에서 지정한다.
UCLASS(BlueprintType)
class MARINEDEMOLITION_API UMD_CharacterRosterData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roster")
	TArray<FMD_CharacterRosterEntry> Characters;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roster")
	TArray<FMD_CharacterPaletteEntry> Palette;

	// MaterialVariants가 없는 캐릭터에서 사용하는 벡터 파라미터
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Roster")
	FName ColorParameterName = TEXT("Color");

	UFUNCTION(BlueprintPure, Category = "MD|Customization")
	bool IsValidCharacterIndex(int32 Index) const { return Characters.IsValidIndex(Index); }

	UFUNCTION(BlueprintPure, Category = "MD|Customization")
	bool IsValidColorIndex(int32 Index) const { return Palette.IsValidIndex(Index); }

	// 범위 밖 인덱스는 0으로 되돌린다. 서버가 클라이언트 값을 받을 때 쓴다
	UFUNCTION(BlueprintPure, Category = "MD|Customization")
	FMD_CharacterAppearance Sanitize(const FMD_CharacterAppearance& Appearance) const;

	// 범위 밖이면 흰색
	UFUNCTION(BlueprintPure, Category = "MD|Customization")
	FLinearColor GetPaletteColor(int32 Index) const;
};
