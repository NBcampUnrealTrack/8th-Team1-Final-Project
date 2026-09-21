#include "Tools/MD_PropPlacementCandidate.h"

#include "Tools/MD_PlacementGeometry.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "MDPropPlacementCandidate"

bool FMD_PropPlacementCandidate::Resolve(UStaticMesh* InMesh, UClass* InActorClass,
	TSoftObjectPtr<UStaticMesh> PreviewMesh, FText& OutError)
{
	Mesh = InMesh;
	ActorClass = InActorClass;
	Parts.Reset();
	ActorScale = FVector::OneVector;
	Settings.Sanitize();
	OutError = FText::GetEmpty();
	if (IsValid(InMesh))
	{
		Parts.AddDefaulted_GetRef().Mesh = InMesh;
	}
	else if (IsValid(InActorClass) && Cast<UBlueprintGeneratedClass>(InActorClass)
		&& InActorClass->IsChildOf(AActor::StaticClass())
		&& !InActorClass->HasAnyClassFlags(CLASS_Abstract | CLASS_NotPlaceable | CLASS_Transient | CLASS_Deprecated | CLASS_NewerVersionExists))
	{
		MDCollectPropPreviewParts(InActorClass, Parts, ActorScale, OutError);
		if (!PreviewMesh.IsNull())
		{
			Parts.Reset();
			if (UStaticMesh* Proxy = PreviewMesh.LoadSynchronous())
			{
				Parts.AddDefaulted_GetRef().Mesh = Proxy;
				OutError = FText::GetEmpty();
			}
			else
			{
				OutError = LOCTEXT("MissingProxy", "Blueprint Preview Mesh를 불러올 수 없습니다. 참조를 수정하고 Refresh Preview를 누르세요.");
			}
		}
	}
	else
	{
		OutError = LOCTEXT("MissingSource", "배치할 메시 또는 컴파일된 액터 BP를 선택하세요.");
	}
	if (OutError.IsEmpty())
	{
		if (Parts.IsEmpty() || ActorScale.ContainsNaN() || ActorScale.GetAbs().GetMin() < UE_SMALL_NUMBER)
		{
			OutError = LOCTEXT("MissingGeometry", "미리보기 외형 또는 기본 크기가 올바르지 않습니다. 에셋이나 Blueprint Preview Mesh를 확인하세요.");
		}
		for (const FMD_PropPlacementPreviewPart& Part : Parts)
		{
			if (!IsValid(Part.Mesh) || !Part.Mesh->GetBoundingBox().IsValid || !Part.LocalTransform.IsValid())
			{
				OutError = LOCTEXT("InvalidGeometry", "미리보기 메시의 경계 또는 컴포넌트 변환이 올바르지 않습니다.");
				break;
			}
		}
	}
	if (!OutError.IsEmpty())
	{
		Parts.Reset();
		return false;
	}
	return true;
}

FTransform FMD_PropPlacementCandidate::BuildTransform(const FHitResult& Hit, uint32 CandidateIndex) const
{
	const FVector Normal = MDPlacementGeometry::GetSurfaceNormal(Hit);
	const FQuat Alignment = Settings.bAlignToSurface
		? FQuat::FindBetweenNormals(FVector::UpVector, Normal) : FQuat::Identity;
	const FQuat Rotation = (Alignment * Settings.EvaluateRotation(CandidateIndex)).GetNormalized();
	FTransform Transform(Rotation, FVector::ZeroVector, ActorScale * Settings.EvaluateScale(CandidateIndex));
	double Lowest = TNumericLimits<double>::Max();
	for (const FMD_PropPlacementPreviewPart& Part : Parts)
	{
		Lowest = FMath::Min(Lowest, MDPlacementGeometry::GetLowestProjection(
			Part.Mesh->GetBoundingBox(), Part.LocalTransform * Transform, Normal));
	}
	Transform.SetLocation(Hit.ImpactPoint + Normal * (Settings.SurfaceOffset - Lowest));
	return Transform;
}

#undef LOCTEXT_NAMESPACE
