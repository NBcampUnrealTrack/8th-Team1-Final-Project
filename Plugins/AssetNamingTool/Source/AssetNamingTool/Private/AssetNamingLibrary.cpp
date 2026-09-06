#include "AssetNamingLibrary.h"

#include "EditorUtilityLibrary.h"

#include "AssetToolsModule.h"
#include "IAssetTools.h"

#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Blueprint.h"

#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"

#include "Misc/PackageName.h"

#include "Editor.h"
#include "Subsystems/EditorAssetSubsystem.h"


// ============================================================
// Prefix 문자열 정리
//
// SM     → SM_
// SM_    → SM_
// " SM " → SM_
// ============================================================

FString UAssetNamingLibrary::NormalizePrefixValue(
    const FString& Prefix
)
{
    FString Result = Prefix;

    Result.TrimStartAndEndInline();

    while (Result.EndsWith(TEXT("_")))
    {
        Result.LeftChopInline(1);
    }

    if (Result.IsEmpty())
    {
        return FString();
    }

    return Result + TEXT("_");
}


// ============================================================
// Descriptor 문자열 정리
//
// _BC → BC
// BC_ → BC
// ============================================================

FString UAssetNamingLibrary::NormalizeDescriptorValue(
    const FString& Descriptor
)
{
    FString Result = Descriptor;

    Result.TrimStartAndEndInline();

    Result.ReplaceInline(TEXT(" "), TEXT("_"));
    Result.ReplaceInline(TEXT("-"), TEXT("_"));

    while (Result.Contains(TEXT("__")))
    {
        Result.ReplaceInline(TEXT("__"), TEXT("_"));
    }

    while (Result.StartsWith(TEXT("_")))
    {
        Result.RightChopInline(1);
    }

    while (Result.EndsWith(TEXT("_")))
    {
        Result.LeftChopInline(1);
    }

    return Result;
}


// ============================================================
// Asset Type → 사용자가 지정한 Prefix
// ============================================================

FString UAssetNamingLibrary::GetRequiredPrefix(
    const UObject* Asset,
    const FAssetNamingSettings& Settings
)
{
    if (Asset == nullptr)
    {
        return FString();
    }

    if (Asset->IsA<UMaterialInstanceConstant>())
    {
        return NormalizePrefixValue(
            Settings.MaterialInstancePrefix
        );
    }

    if (Asset->IsA<UMaterial>())
    {
        return NormalizePrefixValue(
            Settings.MaterialPrefix
        );
    }

    if (Asset->IsA<UStaticMesh>())
    {
        return NormalizePrefixValue(
            Settings.StaticMeshPrefix
        );
    }

    if (Asset->IsA<USkeletalMesh>())
    {
        return NormalizePrefixValue(
            Settings.SkeletalMeshPrefix
        );
    }

    if (Asset->IsA<UTexture>())
    {
        return NormalizePrefixValue(
            Settings.TexturePrefix
        );
    }

    if (Asset->IsA<UBlueprint>())
    {
        return NormalizePrefixValue(
            Settings.BlueprintPrefix
        );
    }

    return FString();
}


// ============================================================
// 기존 / 잘못된 Prefix 제거
// ============================================================

FString UAssetNamingLibrary::RemoveKnownPrefix(
    const FString& Name,
    const FAssetNamingSettings& Settings
)
{
    TArray<FString> KnownPrefixes =
    {
        // 현재 사용자가 지정한 Prefix
        NormalizePrefixValue(Settings.StaticMeshPrefix),
        NormalizePrefixValue(Settings.SkeletalMeshPrefix),
        NormalizePrefixValue(Settings.MaterialPrefix),
        NormalizePrefixValue(Settings.MaterialInstancePrefix),
        NormalizePrefixValue(Settings.TexturePrefix),
        NormalizePrefixValue(Settings.BlueprintPrefix),

        // Unreal 일반 Convention
        TEXT("SM_"),
        TEXT("SK_"),
        TEXT("MI_"),
        TEXT("M_"),
        TEXT("T_"),
        TEXT("BP_"),

        // DCC / 비표준 Naming
        TEXT("StaticMesh_"),
        TEXT("SkeletalMesh_"),
        TEXT("MaterialInstance_"),
        TEXT("Material_"),
        TEXT("Texture_"),
        TEXT("Blueprint_"),

        TEXT("Mesh_"),
        TEXT("Mat_"),
        TEXT("Tex_")
    };


    // 빈 Prefix 제거
    KnownPrefixes.RemoveAll(
        [](const FString& Prefix)
        {
            return Prefix.IsEmpty();
        }
    );


    // MI_보다 M_가 먼저 검사되는 문제 등을 방지하기 위해
    // 긴 Prefix부터 검사
    KnownPrefixes.Sort(
        [](const FString& A, const FString& B)
        {
            return A.Len() > B.Len();
        }
    );


    FString Result = Name;

    bool bRemovedPrefix = true;

    while (bRemovedPrefix)
    {
        bRemovedPrefix = false;

        for (const FString& Prefix : KnownPrefixes)
        {
            if (Result.StartsWith(
                Prefix,
                ESearchCase::IgnoreCase
            ))
            {
                Result.RightChopInline(
                    Prefix.Len()
                );

                bRemovedPrefix = true;

                break;
            }
        }
    }

    return Result;
}


// ============================================================
// Blender Suffix
//
// Chair.001
// Chair_001
//
// → Chair_01
// ============================================================

FString UAssetNamingLibrary::NormalizeBlenderSuffix(
    const FString& Name
)
{
    FString Result = Name;


    // Blender 원본 .001
    int32 DotIndex = INDEX_NONE;

    if (Result.FindLastChar(TEXT('.'), DotIndex))
    {
        const FString NumberPart =
            Result.Mid(DotIndex + 1);

        if (NumberPart.Len() == 3 &&
            NumberPart.IsNumeric())
        {
            const int32 Number =
                FCString::Atoi(*NumberPart);

            return Result.Left(DotIndex)
                + TEXT("_")
                + FString::Printf(
                    TEXT("%02d"),
                    Number
                );
        }
    }


    // Unreal Import 후 _001
    int32 UnderscoreIndex = INDEX_NONE;

    if (Result.FindLastChar(
        TEXT('_'),
        UnderscoreIndex
    ))
    {
        const FString NumberPart =
            Result.Mid(UnderscoreIndex + 1);

        if (NumberPart.Len() == 3 &&
            NumberPart.IsNumeric() &&
            NumberPart.StartsWith(TEXT("0")))
        {
            const int32 Number =
                FCString::Atoi(*NumberPart);

            Result =
                Result.Left(UnderscoreIndex)
                + TEXT("_")
                + FString::Printf(
                    TEXT("%02d"),
                    Number
                );
        }
    }

    return Result;
}


// ============================================================
// Separator 정규화
// ============================================================

FString UAssetNamingLibrary::SanitizeBaseName(
    const FString& Name
)
{
    FString Result = Name;

    Result.ReplaceInline(TEXT(" "), TEXT("_"));
    Result.ReplaceInline(TEXT("-"), TEXT("_"));
    Result.ReplaceInline(TEXT("."), TEXT("_"));

    while (Result.Contains(TEXT("__")))
    {
        Result.ReplaceInline(TEXT("__"), TEXT("_"));
    }

    while (Result.StartsWith(TEXT("_")))
    {
        Result.RightChopInline(1);
    }

    while (Result.EndsWith(TEXT("_")))
    {
        Result.LeftChopInline(1);
    }

    return Result;
}


// ============================================================
// wall_final → Wall_Final
// ============================================================

FString UAssetNamingLibrary::NormalizeWordCase(
    const FString& Name
)
{
    TArray<FString> Words;

    Name.ParseIntoArray(
        Words,
        TEXT("_"),
        true
    );

    for (FString& Word : Words)
    {
        if (Word.IsEmpty())
        {
            continue;
        }

        if (Word.IsNumeric())
        {
            continue;
        }

        if (Word.Equals(
            TEXT("AO"),
            ESearchCase::IgnoreCase
        ))
        {
            Word = TEXT("AO");
            continue;
        }

        if (Word.Equals(
            TEXT("UV"),
            ESearchCase::IgnoreCase
        ))
        {
            Word = TEXT("UV");
            continue;
        }

        if (Word.Equals(
            TEXT("LOD"),
            ESearchCase::IgnoreCase
        ))
        {
            Word = TEXT("LOD");
            continue;
        }

        Word =
            Word.Left(1).ToUpper()
            + Word.Mid(1);
    }

    return FString::Join(
        Words,
        TEXT("_")
    );
}


// ============================================================
// Texture Descriptor
// ============================================================

FString UAssetNamingLibrary::NormalizeTextureDescriptor(
    const FString& Name,
    const FAssetNamingSettings& Settings
)
{
    struct FTextureDescriptorRule
    {
        FString SourceSuffix;
        FString TargetSuffix;
    };


    const FString BC =
        NormalizeDescriptorValue(
            Settings.BaseColorDescriptor
        );

    const FString N =
        NormalizeDescriptorValue(
            Settings.NormalDescriptor
        );

    const FString R =
        NormalizeDescriptorValue(
            Settings.RoughnessDescriptor
        );

    const FString AO =
        NormalizeDescriptorValue(
            Settings.AODescriptor
        );


    const TArray<FTextureDescriptorRule> Rules =
    {
        // BaseColor
        { TEXT("_base_color"), BC },
        { TEXT("_basecolor"),  BC },
        { TEXT("_albedo"),     BC },
        { TEXT("_diffuse"),    BC },

        // Normal
        { TEXT("_normalgl"), N },
        { TEXT("_normaldx"), N },
        { TEXT("_normal"),   N },

        // Roughness
        { TEXT("_roughness"), R },

        // AO
        { TEXT("_ambient_occlusion"), AO },
        { TEXT("_ao"), AO },

        // 이미 현재 Convention으로 된 경우
        { TEXT("_bc"), BC },
        { TEXT("_n"),  N },
        { TEXT("_r"),  R }
    };


    for (const FTextureDescriptorRule& Rule : Rules)
    {
        if (Name.EndsWith(
            Rule.SourceSuffix,
            ESearchCase::IgnoreCase
        ))
        {
            FString RootName =
                Name.LeftChop(
                    Rule.SourceSuffix.Len()
                );

            while (RootName.EndsWith(TEXT("_")))
            {
                RootName.LeftChopInline(1);
            }

            if (Settings.bNormalizeWordCase)
            {
                RootName =
                    NormalizeWordCase(
                        RootName
                    );
            }

            if (Rule.TargetSuffix.IsEmpty())
            {
                return RootName;
            }

            return RootName
                + TEXT("_")
                + Rule.TargetSuffix;
        }
    }


    if (Settings.bNormalizeWordCase)
    {
        return NormalizeWordCase(Name);
    }

    return Name;
}


// ============================================================
// 최종 이름 생성
// ============================================================

FString UAssetNamingLibrary::BuildProposedName(
    const UObject* Asset,
    const FAssetNamingSettings& Settings
)
{
    if (Asset == nullptr)
    {
        return FString();
    }


    const FString RequiredPrefix =
        GetRequiredPrefix(
            Asset,
            Settings
        );


    if (RequiredPrefix.IsEmpty())
    {
        return Asset->GetName();
    }


    FString BaseName =
        RemoveKnownPrefix(
            Asset->GetName(),
            Settings
        );


    if (Settings.bRemoveCharacters)
    {
        BaseName =
            RemoveSpecifiedCharacters(
                BaseName,
                Settings.CharactersToRemove
            );
    }


    if (Settings.bRemoveWords)
    {
        BaseName =
            RemoveSpecifiedWords(
                BaseName,
                Settings.WordsToRemove
            );
    }


    if (Settings.bNormalizeBlenderSuffix)
    {
        BaseName =
            NormalizeBlenderSuffix(
                BaseName
            );
    }


    if (Settings.bNormalizeSeparators)
    {
        BaseName =
            SanitizeBaseName(
                BaseName
            );
    }


    if (Asset->IsA<UTexture>() &&
        Settings.bNormalizeTextureDescriptors)
    {
        BaseName =
            NormalizeTextureDescriptor(
                BaseName,
                Settings
            );
    }
    else if (Settings.bNormalizeWordCase)
    {
        BaseName =
            NormalizeWordCase(
                BaseName
            );
    }


    return RequiredPrefix + BaseName;
}


// ============================================================
// Preview
// ============================================================

TArray<FAssetRenamePreview>
UAssetNamingLibrary::BuildSelectedAssetPreview(
    const FAssetNamingSettings& Settings
)
{
    TArray<FAssetRenamePreview> PreviewList;


    const TArray<UObject*> SelectedAssets =
        UEditorUtilityLibrary::GetSelectedAssets();


    UEditorAssetSubsystem* AssetSubsystem = nullptr;

    if (GEditor != nullptr)
    {
        AssetSubsystem =
            GEditor->GetEditorSubsystem<
            UEditorAssetSubsystem
            >();
    }


    TMap<FString, int32> ProposedPathCounts;


    // --------------------------------------------------------
    // 여러 Asset이 같은 이름을 요구하는지 먼저 계산
    // --------------------------------------------------------

    for (UObject* Asset : SelectedAssets)
    {
        if (Asset == nullptr)
        {
            continue;
        }


        const FString Prefix =
            GetRequiredPrefix(
                Asset,
                Settings
            );


        if (Prefix.IsEmpty())
        {
            continue;
        }


        const FString ProposedName =
            BuildProposedName(
                Asset,
                Settings
            );


        if (ProposedName == Asset->GetName())
        {
            continue;
        }


        const FString PackageName =
            Asset->GetOutermost()->GetName();


        const FString PackagePath =
            FPackageName::GetLongPackagePath(
                PackageName
            );


        const FString ProposedAssetPath =
            PackagePath
            + TEXT("/")
            + ProposedName;


        ProposedPathCounts.FindOrAdd(
            ProposedAssetPath
        )++;
    }


    // --------------------------------------------------------
    // Preview 생성
    // --------------------------------------------------------

    for (UObject* Asset : SelectedAssets)
    {
        if (Asset == nullptr)
        {
            continue;
        }


        FAssetRenamePreview Preview;

        Preview.Asset =
            Asset;

        Preview.CurrentName =
            Asset->GetName();

        Preview.AssetType =
            Asset->GetClass()->GetName();


        const FString Prefix =
            GetRequiredPrefix(
                Asset,
                Settings
            );


        // Unsupported
        if (Prefix.IsEmpty())
        {
            Preview.ProposedName =
                Preview.CurrentName;

            Preview.bWillRename = false;

            Preview.Status =
                EAssetNamingStatus::Unsupported;

            Preview.Message =
                TEXT("Unsupported Asset Type");

            PreviewList.Add(Preview);

            continue;
        }


        Preview.ProposedName =
            BuildProposedName(
                Asset,
                Settings
            );


        // Valid
        if (Preview.CurrentName ==
            Preview.ProposedName)
        {
            Preview.bWillRename = false;

            Preview.Status =
                EAssetNamingStatus::Valid;

            Preview.Message =
                TEXT("Valid");

            PreviewList.Add(Preview);

            continue;
        }


        const FString PackageName =
            Asset->GetOutermost()->GetName();


        const FString PackagePath =
            FPackageName::GetLongPackagePath(
                PackageName
            );


        const FString ProposedAssetPath =
            PackagePath
            + TEXT("/")
            + Preview.ProposedName;


        // 선택 항목끼리 이름 충돌
        const int32* ProposedCount =
            ProposedPathCounts.Find(
                ProposedAssetPath
            );


        if (ProposedCount != nullptr &&
            *ProposedCount > 1)
        {
            Preview.bWillRename = false;

            Preview.Status =
                EAssetNamingStatus::Conflict;

            Preview.Message =
                TEXT("Conflict: Duplicate Proposed Name");

            PreviewList.Add(Preview);

            continue;
        }


        // 기존 Asset과 충돌
        if (AssetSubsystem != nullptr &&
            AssetSubsystem->DoesAssetExist(
                ProposedAssetPath
            ))
        {
            Preview.bWillRename = false;

            Preview.Status =
                EAssetNamingStatus::Conflict;

            Preview.Message =
                TEXT("Conflict: Asset Already Exists");

            PreviewList.Add(Preview);

            continue;
        }


        Preview.bWillRename = true;

        Preview.Status =
            EAssetNamingStatus::NeedsRename;

        Preview.Message =
            TEXT("Needs Rename");

        PreviewList.Add(Preview);
    }


    return PreviewList;
}


// ============================================================
// Rename
// ============================================================

bool UAssetNamingLibrary::RenameSelectedAssets(
    const FAssetNamingSettings& Settings,
    int32& OutRenamedCount,
    TArray<FString>& OutErrors
)
{
    OutRenamedCount = 0;
    OutErrors.Empty();


    const TArray<FAssetRenamePreview> PreviewList =
        BuildSelectedAssetPreview(
            Settings
        );


    if (PreviewList.IsEmpty())
    {
        OutErrors.Add(
            TEXT("No assets are selected.")
        );

        return false;
    }


    TArray<FAssetRenameData> RenameDataList;


    for (const FAssetRenamePreview& Preview : PreviewList)
    {
        if (Preview.Asset == nullptr)
        {
            continue;
        }


        if (Preview.Status ==
            EAssetNamingStatus::Conflict)
        {
            OutErrors.Add(
                FString::Printf(
                    TEXT("%s : Naming conflict."),
                    *Preview.CurrentName
                )
            );

            continue;
        }


        if (Preview.Status ==
            EAssetNamingStatus::Unsupported)
        {
            OutErrors.Add(
                FString::Printf(
                    TEXT("%s : Unsupported asset type."),
                    *Preview.CurrentName
                )
            );

            continue;
        }


        if (Preview.Status ==
            EAssetNamingStatus::Valid)
        {
            continue;
        }


        if (Preview.Status ==
            EAssetNamingStatus::NeedsRename)
        {
            const FString PackageName =
                Preview.Asset
                ->GetOutermost()
                ->GetName();


            const FString PackagePath =
                FPackageName::GetLongPackagePath(
                    PackageName
                );


            RenameDataList.Emplace(
                Preview.Asset,
                PackagePath,
                Preview.ProposedName
            );
        }
    }


    if (RenameDataList.IsEmpty())
    {
        return OutErrors.IsEmpty();
    }


    IAssetTools& AssetTools =
        FAssetToolsModule::GetModule().Get();


    const bool bSuccess =
        AssetTools.RenameAssets(
            RenameDataList
        );


    if (bSuccess)
    {
        OutRenamedCount =
            RenameDataList.Num();

        return true;
    }


    OutErrors.Add(
        TEXT(
            "AssetTools failed to rename "
            "one or more assets."
        )
    );

    return false;
}

FString UAssetNamingLibrary::RemoveSpecifiedCharacters(
    const FString& Name,
    const FString& CharactersToRemove
)
{
    FString Result = Name;

    for (const TCHAR Character : CharactersToRemove)
    {
        const FString CharacterString =
            FString::Chr(Character);

        Result.ReplaceInline(
            *CharacterString,
            TEXT("")
        );
    }

    return Result;
}

FString UAssetNamingLibrary::RemoveSpecifiedWords(
    const FString& Name,
    const FString& WordsToRemove
)
{
    FString Result = Name;

    TArray<FString> Words;

    WordsToRemove.ParseIntoArray(
        Words,
        TEXT(","),
        true
    );

    for (FString& Word : Words)
    {
        Word.TrimStartAndEndInline();

        if (Word.IsEmpty())
        {
            continue;
        }

        Result.ReplaceInline(
            *Word,
            TEXT(""),
            ESearchCase::IgnoreCase
        );
    }

    return Result;
}