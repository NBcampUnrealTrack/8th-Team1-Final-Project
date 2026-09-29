#include "World/Trash/MD_TrashSorterAnimationComponent.h"

#include "Components/StaticMeshComponent.h"
#include "Curves/CurveFloat.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

UMD_TrashSorterAnimationComponent::UMD_TrashSorterAnimationComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

FMD_SorterItemVisual UMD_TrashSorterAnimationComponent::CaptureItem(const AActor* Item)
{
	FMD_SorterItemVisual Result;
	const UStaticMeshComponent* Source = Item ? Item->FindComponentByClass<UStaticMeshComponent>() : nullptr;
	if (!Source || !Source->GetStaticMesh()) return Result;
	Result.Mesh = Source->GetStaticMesh();
	Result.Transform = Source->GetComponentTransform();
	Result.CustomPrimitiveData = Source->GetCustomPrimitiveData().Data;
	for (int32 Slot = 0; Slot < Source->GetNumMaterials(); ++Slot)
	{
		UMaterialInterface* Material = Source->GetMaterial(Slot);
		// Runtime MIDs cannot be sent over the network. Preserve their vector overrides
		// (including the wall piece's contamination tint) and send the asset parent.
		while (const UMaterialInstanceDynamic* Dynamic = Cast<UMaterialInstanceDynamic>(Material))
		{
			for (const FVectorParameterValue& Parameter : Dynamic->VectorParameterValues)
			{
				if (!Result.VectorParameters.ContainsByPredicate([&](const FMD_SorterMaterialVector& Existing)
					{ return Existing.Slot == Slot && Existing.Name == Parameter.ParameterInfo.Name; }))
				{
					FMD_SorterMaterialVector& Copy = Result.VectorParameters.AddDefaulted_GetRef();
					Copy.Slot = Slot;
					Copy.Name = Parameter.ParameterInfo.Name;
					Copy.Value = Parameter.ParameterValue;
				}
			}
			Material = Dynamic->Parent;
		}
		Result.Materials.Add(Material);
	}
	return Result;
}

void UMD_TrashSorterAnimationComponent::StartSorting(const FMD_SorterItemVisual& Item, float Duration, float Elapsed)
{
	StopSorting();
	if (!bEnabled || GetNetMode() == NM_DedicatedServer || Duration <= 0.0f || Elapsed >= Duration) return;
	AnimationDuration = Duration;
	AnimationElapsed = FMath::Clamp(Elapsed, 0.0f, Duration);
	const FTransform& SorterTransform = GetOwner()->GetActorTransform();
	const UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(GetOwner()->GetRootComponent());
	FVector RollerCenter = Body ? SorterTransform.InverseTransformPosition(Body->Bounds.Origin) : FVector::ZeroVector;
	TInlineComponentArray<UStaticMeshComponent*> Meshes(GetOwner());
	for (UStaticMeshComponent* Mesh : Meshes)
	{
		if (Mesh->GetFName() == BladeComponentName)
		{
			Blade = Mesh;
			BladeStart = Mesh->GetRelativeTransform();
			RollerCenter = SorterTransform.InverseTransformPosition(Mesh->GetComponentTransform().TransformPosition(BladePivot));
			break;
		}
	}
	FeedEntry = RollerCenter + FeedEntryOffset;
	FeedExit = RollerCenter + FeedExitOffset;
	if (const UStaticMeshComponent* Roller = Blade.Get())
	{
		const FVector Axis = SorterTransform.InverseTransformVectorNoScale(
			Roller->GetComponentTransform().TransformVectorNoScale(BladeAxis.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector)));
		const FVector SurfaceTravel = FVector::CrossProduct(Axis, FeedEntry - RollerCenter);
		// Reverse the feed-derived rotation to match the requested blade direction.
		SignedBladeTurns = FMath::Abs(BladeTurns) * (FVector::DotProduct(SurfaceTravel, FeedExit - FeedEntry) < 0.0 ? 1.0f : -1.0f);
	}
	if (Item.Mesh)
	{
		IntakeVisual = NewObject<UStaticMeshComponent>(GetOwner(), NAME_None, RF_Transient);
		IntakeVisual->SetupAttachment(GetOwner()->GetRootComponent());
		IntakeVisual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		IntakeVisual->SetGenerateOverlapEvents(false);
		IntakeVisual->SetCanEverAffectNavigation(false);
		IntakeVisual->SetStaticMesh(Item.Mesh);
		for (int32 Slot = 0; Slot < Item.Materials.Num(); ++Slot) IntakeVisual->SetMaterial(Slot, Item.Materials[Slot]);
		TMap<int32, UMaterialInstanceDynamic*> Overrides;
		for (const FMD_SorterMaterialVector& Parameter : Item.VectorParameters)
		{
			UMaterialInstanceDynamic*& Material = Overrides.FindOrAdd(Parameter.Slot);
			if (!Material) Material = IntakeVisual->CreateAndSetMaterialInstanceDynamic(Parameter.Slot);
			if (Material) Material->SetVectorParameterValue(Parameter.Name, Parameter.Value);
		}
		for (int32 Index = 0; Index < Item.CustomPrimitiveData.Num(); ++Index)
			IntakeVisual->SetCustomPrimitiveDataFloat(Index, Item.CustomPrimitiveData[Index]);
		IntakeVisual->RegisterComponent();
		ItemStart = Item.Transform.GetRelativeTransform(SorterTransform);
		ItemLocalCenter = Item.Mesh->GetBounds().Origin;
		ItemStartCenter = ItemStart.TransformPosition(ItemLocalCenter);
	}
	ApplyPose(AnimationElapsed / AnimationDuration);
	SetComponentTickEnabled(Blade.IsValid() || IntakeVisual != nullptr);
}

void UMD_TrashSorterAnimationComponent::ApplyPose(float Alpha)
{
	if (UStaticMeshComponent* Mesh = Blade.Get())
	{
		const float Progress = BladeCurve ? BladeCurve->GetFloatValue(Alpha) : Alpha;
		const FQuat Spin(BladeAxis.GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector),
			FMath::DegreesToRadians(360.0f * SignedBladeTurns * Progress));
		FTransform Pose = BladeStart;
		Pose.SetRotation(BladeStart.GetRotation() * Spin);
		// Keep the authored shaft center stationary even when the mesh pivot is off-center.
		Pose.SetTranslation(BladeStart.TransformPosition(BladePivot)
			- Pose.TransformVector(BladePivot));
		Mesh->SetRelativeTransform(Pose);
	}
	if (IntakeVisual)
	{
		const float Progress = FMath::Clamp(IntakeCurve ? IntakeCurve->GetFloatValue(Alpha) : Alpha, 0.0f, 1.0f);
		const float ApproachEnd = FMath::Clamp(ApproachFraction, 0.05f, 0.95f);
		const float FeedProgress = FMath::Clamp((Progress - ApproachEnd) / (1.0f - ApproachEnd), 0.0f, 1.0f);
		// No arc or upward overshoot: pull to the lip, then travel under the roller.
		const FVector Position = Progress < ApproachEnd
			? FMath::Lerp(ItemStartCenter, FeedEntry, Progress / ApproachEnd)
			: FMath::Lerp(FeedEntry, FeedExit, FeedProgress);
		const float Compression = FMath::SmoothStep(0.35f, 1.0f, FeedProgress);
		FTransform Pose = ItemStart;
		Pose.SetScale3D(ItemStart.GetScale3D() * FMath::Lerp(1.0f, EndScale, Compression));
		// Follow the visual bounds center rather than an asset's potentially off-center pivot.
		Pose.SetLocation(Position - Pose.TransformVector(ItemLocalCenter));
		IntakeVisual->SetRelativeTransform(Pose);
	}
}

void UMD_TrashSorterAnimationComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AnimationElapsed = FMath::Min(AnimationElapsed + DeltaTime, AnimationDuration);
	ApplyPose(AnimationElapsed / AnimationDuration);
	if (AnimationElapsed >= AnimationDuration) StopSorting();
}

void UMD_TrashSorterAnimationComponent::StopSorting()
{
	SetComponentTickEnabled(false);
	Blade.Reset();
	if (IntakeVisual)
	{
		IntakeVisual->DestroyComponent();
		IntakeVisual = nullptr;
	}
}

void UMD_TrashSorterAnimationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSorting();
	Super::EndPlay(EndPlayReason);
}
