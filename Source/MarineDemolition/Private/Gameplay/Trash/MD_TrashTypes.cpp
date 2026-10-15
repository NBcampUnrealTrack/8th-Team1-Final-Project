#include "Types/Trash/MD_TrashTypes.h"

#include "Engine/StaticMesh.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

FMD_TrashKindRow::FMD_TrashKindRow()
{
	MeshVariants.AddDefaulted_GetRef().VariantId = TEXT("Default");
}

void FMD_TrashKindRow::MigrateLegacyMesh()
{
	if (!Mesh_DEPRECATED.IsNull())
	{
		const bool bDefaultPlaceholder = MeshVariants.Num() == 1
			&& MeshVariants[0].VariantId == TEXT("Default") && MeshVariants[0].Mesh.IsNull();
		if (MeshVariants.IsEmpty() || bDefaultPlaceholder)
		{
			MeshVariants.Reset();
			FMD_TrashMeshVariant& Variant = MeshVariants.AddDefaulted_GetRef();
			Variant.VariantId = TEXT("Default");
			Variant.Mesh = Mesh_DEPRECATED;
		}
		else if (MeshVariants[0].Mesh != Mesh_DEPRECATED)
		{
			// 기존 이름/메시를 덮어쓰지 않고 이전 기본 메시를 첫 항목에 보존한다.
			FName LegacyId(TEXT("Default"));
			int32 Suffix = 1;
			while (MeshVariants.ContainsByPredicate([LegacyId](const FMD_TrashMeshVariant& V) { return V.VariantId == LegacyId; }))
			{
				LegacyId = FName(*FString::Printf(TEXT("LegacyDefault%d"), Suffix++));
			}
			FMD_TrashMeshVariant LegacyVariant;
			LegacyVariant.VariantId = LegacyId;
			LegacyVariant.Mesh = Mesh_DEPRECATED;
			MeshVariants.Insert(LegacyVariant, 0);
		}
		Mesh_DEPRECATED.Reset();
	}
	if (MeshVariants.IsEmpty()) MeshVariants.AddDefaulted_GetRef().VariantId = TEXT("Default");
}

void FMD_TrashKindRow::PostSerialize(const FArchive& Ar)
{
	if (Ar.IsLoading()) MigrateLegacyMesh();
}

void FMD_TrashKindRow::OnPostDataImport(const UDataTable* InDataTable, FName InRowName,
	TArray<FString>& OutCollectedImportProblems)
{
	MigrateLegacyMesh();
}

FName FMD_TrashKindRow::GetDefaultVariantId() const
{
	if (MeshVariants.IsEmpty() || MeshVariants[0].VariantId.IsNone()) return NAME_None;
	TSoftObjectPtr<UStaticMesh> Reference;
	return ResolveMesh(MeshVariants[0].VariantId, Reference) ? MeshVariants[0].VariantId : NAME_None;
}

TArray<FName> FMD_TrashKindRow::GetVariantIds() const
{
	TSet<FName> Seen;
	TSet<FName> Duplicates;
	for (const FMD_TrashMeshVariant& Variant : MeshVariants)
	{
		if (Seen.Contains(Variant.VariantId)) Duplicates.Add(Variant.VariantId);
		Seen.Add(Variant.VariantId);
	}

	TArray<FName> Ids;
	for (const FMD_TrashMeshVariant& Variant : MeshVariants)
	{
		if (!Variant.VariantId.IsNone() && !Variant.Mesh.IsNull() && !Duplicates.Contains(Variant.VariantId))
		{
			Ids.Add(Variant.VariantId);
		}
	}
	// 같은 Seed는 테이블 배열 순서와 무관하게 같은 이름을 선택한다.
	Ids.Sort([](FName A, FName B) { return A.LexicalLess(B); });
	return Ids;
}

bool FMD_TrashKindRow::ResolveMesh(FName VariantId, TSoftObjectPtr<UStaticMesh>& OutMesh) const
{
	OutMesh.Reset();
	if (VariantId.IsNone())
	{
		if (MeshVariants.IsEmpty() || MeshVariants[0].VariantId.IsNone()) return false;
		VariantId = MeshVariants[0].VariantId;
	}

	const FMD_TrashMeshVariant* Match = nullptr;
	for (const FMD_TrashMeshVariant& Variant : MeshVariants)
	{
		if (Variant.VariantId == VariantId)
		{
			if (Match) return false; // 중복 이름은 어느 메시인지 모호하므로 거절한다.
			Match = &Variant;
		}
	}
	if (!Match || Match->Mesh.IsNull()) return false;
	OutMesh = Match->Mesh;
	return true;
}

float FMD_TrashKindRow::ResolveMassKg(FName VariantId) const
{
	const float DefaultMassKg = FMath::IsFinite(MassKg) && MassKg >= 0.01f ? MassKg : 1.0f;
	if (VariantId.IsNone())
	{
		if (MeshVariants.IsEmpty() || MeshVariants[0].VariantId.IsNone()) return DefaultMassKg;
		VariantId = MeshVariants[0].VariantId;
	}

	const FMD_TrashMeshVariant* Match = nullptr;
	for (const FMD_TrashMeshVariant& Variant : MeshVariants)
	{
		if (Variant.VariantId == VariantId)
		{
			if (Match) return DefaultMassKg;
			Match = &Variant;
		}
	}
	if (!Match || Match->Mesh.IsNull() || !FMath::IsFinite(Match->MassKgOverride)
		|| Match->MassKgOverride <= 0.0f)
	{
		return DefaultMassKg;
	}
	return FMath::Max(Match->MassKgOverride, 0.01f);
}

#if WITH_EDITOR
EDataValidationResult FMD_TrashKindRow::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = EDataValidationResult::Valid;
	if (!FMath::IsFinite(MassKg) || MassKg < 0.01f)
	{
		Context.AddError(NSLOCTEXT("MDTrash", "InvalidKindMass", "Kind mass must be finite and at least 0.01 kg."));
		Result = EDataValidationResult::Invalid;
	}
	if (MeshVariants.IsEmpty())
	{
		Context.AddError(NSLOCTEXT("MDTrash", "MissingDefaultVariant", "At least one mesh Variant is required."));
		Result = EDataValidationResult::Invalid;
	}
	TSet<FName> Seen;
	for (const FMD_TrashMeshVariant& Variant : MeshVariants)
	{
		if (Variant.VariantId.IsNone() || Seen.Contains(Variant.VariantId) || Variant.Mesh.IsNull())
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("MDTrash", "InvalidMeshVariant", "Variant '{0}' requires a unique non-None name and a mesh."),
				FText::FromName(Variant.VariantId)));
			Result = EDataValidationResult::Invalid;
		}
		if (!FMath::IsFinite(Variant.MassKgOverride) || Variant.MassKgOverride < 0.0f)
		{
			Context.AddError(FText::Format(
				NSLOCTEXT("MDTrash", "InvalidVariantMass", "Variant '{0}' mass override must be finite and non-negative; 0 inherits the kind mass."),
				FText::FromName(Variant.VariantId)));
			Result = EDataValidationResult::Invalid;
		}
		Seen.Add(Variant.VariantId);
	}
	return Result;
}
#endif
