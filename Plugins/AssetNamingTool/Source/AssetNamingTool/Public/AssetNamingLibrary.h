#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AssetNamingLibrary.generated.h"


UENUM(BlueprintType)
enum class EAssetNamingStatus : uint8
{
    Valid       UMETA(DisplayName = "Valid"),
    NeedsRename UMETA(DisplayName = "Needs Rename"),
    Conflict    UMETA(DisplayName = "Conflict"),
    Unsupported UMETA(DisplayName = "Unsupported")
};


// ============================================================
// 사용자가 Editor Utility Widget에서 설정할 Naming Rule
// ============================================================

USTRUCT(BlueprintType)
struct FAssetNamingSettings
{
    GENERATED_BODY()

    // --------------------------------------------------------
    // Normalization
    // --------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normalization")
    bool bNormalizeBlenderSuffix = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normalization")
    bool bNormalizeSeparators = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normalization")
    bool bNormalizeWordCase = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normalization")
    bool bNormalizeTextureDescriptors = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normalization")
    bool bRemoveCharacters = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normalization")
    FString CharactersToRemove = TEXT("()[]{}#@$%");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normalization")
    bool bRemoveWords = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Normalization")
    FString WordsToRemove = TEXT("Tripo");

    // --------------------------------------------------------
    // Prefix
    // --------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prefix")
    FString StaticMeshPrefix = TEXT("SM_");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prefix")
    FString SkeletalMeshPrefix = TEXT("SK_");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prefix")
    FString MaterialPrefix = TEXT("M_");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prefix")
    FString MaterialInstancePrefix = TEXT("MI_");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prefix")
    FString TexturePrefix = TEXT("T_");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Prefix")
    FString BlueprintPrefix = TEXT("BP_");


    // --------------------------------------------------------
    // Texture Descriptor
    // --------------------------------------------------------

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Descriptor")
    FString BaseColorDescriptor = TEXT("BC");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Descriptor")
    FString NormalDescriptor = TEXT("N");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Descriptor")
    FString RoughnessDescriptor = TEXT("R");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Texture Descriptor")
    FString AODescriptor = TEXT("AO");
};


USTRUCT(BlueprintType)
struct FAssetRenamePreview
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Asset Naming")
    TObjectPtr<UObject> Asset = nullptr;

    UPROPERTY(BlueprintReadOnly, Category = "Asset Naming")
    FString CurrentName;

    UPROPERTY(BlueprintReadOnly, Category = "Asset Naming")
    FString ProposedName;

    UPROPERTY(BlueprintReadOnly, Category = "Asset Naming")
    FString AssetType;

    UPROPERTY(BlueprintReadOnly, Category = "Asset Naming")
    bool bWillRename = false;

    UPROPERTY(BlueprintReadOnly, Category = "Asset Naming")
    FString Message;

    UPROPERTY(BlueprintReadOnly, Category = "Asset Naming")
    EAssetNamingStatus Status = EAssetNamingStatus::Valid;
};


UCLASS()
class ASSETNAMINGTOOL_API UAssetNamingLibrary
    : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:

    UFUNCTION(
        BlueprintCallable,
        Category = "Asset Naming Tool"
    )
    static TArray<FAssetRenamePreview> BuildSelectedAssetPreview(
        const FAssetNamingSettings& Settings
    );


    UFUNCTION(
        BlueprintCallable,
        Category = "Asset Naming Tool"
    )
    static bool RenameSelectedAssets(
        const FAssetNamingSettings& Settings,
        int32& OutRenamedCount,
        TArray<FString>& OutErrors
    );


private:

    static FString GetRequiredPrefix(
        const UObject* Asset,
        const FAssetNamingSettings& Settings
    );

    static FString NormalizePrefixValue(
        const FString& Prefix
    );

    static FString NormalizeDescriptorValue(
        const FString& Descriptor
    );

    static FString RemoveKnownPrefix(
        const FString& Name,
        const FAssetNamingSettings& Settings
    );

    static FString NormalizeBlenderSuffix(
        const FString& Name
    );

    static FString SanitizeBaseName(
        const FString& Name
    );

    static FString NormalizeWordCase(
        const FString& Name
    );

    static FString NormalizeTextureDescriptor(
        const FString& Name,
        const FAssetNamingSettings& Settings
    );

    static FString BuildProposedName(
        const UObject* Asset,
        const FAssetNamingSettings& Settings
    );

    static FString RemoveSpecifiedCharacters(
        const FString& Name,
        const FString& CharactersToRemove
    );

    static FString RemoveSpecifiedWords(
        const FString& Name,
        const FString& WordsToRemove
    );
};