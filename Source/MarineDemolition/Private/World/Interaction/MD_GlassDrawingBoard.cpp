#include "World/Interaction/MD_GlassDrawingBoard.h"

#include "Algo/Sort.h"
#include "Camera/CameraComponent.h"
#include "Character/MD_Character.h"
#include "Component/Interaction/MD_GlassDrawingFocusComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Drawing/MD_GlassDrawingProfile.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "RHI.h"
#include "Subsystem/World/MD_GlassDrawingSubsystem.h"
#include "TimerManager.h"

namespace MDGlassDrawing
{
	constexpr double CoordinateMax = 65535.0;

	uint16 Quantize(double Value)
	{
		return uint16(FMath::RoundToInt(FMath::Clamp(Value, 0.0, 1.0) * CoordinateMax));
	}

	struct FClip
	{
		int32 Index = INDEX_NONE;
		TArray<TPair<FVector2D, FVector2D>, TInlineAllocator<2>> Kept;
	};

	/** Return false if the disk does not remove any of this segment. Coordinates are physical cm. */
	bool ClipAgainstDisk(const FVector2D& A, const FVector2D& B, const FVector2D& Center,
		double Radius, FClip& Out)
	{
		const FVector2D Delta = B - A;
		const FVector2D Offset = A - Center;
		const double LengthSquared = Delta.SizeSquared();
		if (LengthSquared <= UE_SMALL_NUMBER)
		{
			return Offset.SizeSquared() <= FMath::Square(Radius);
		}
		const double Linear = 2.0 * FVector2D::DotProduct(Offset, Delta);
		const double Constant = Offset.SizeSquared() - FMath::Square(Radius);
		const double Discriminant = Linear * Linear - 4.0 * LengthSquared * Constant;
		if (Discriminant <= 0.0) return false;
		const double Root = FMath::Sqrt(Discriminant);
		const double Entry = FMath::Clamp((-Linear - Root) / (2.0 * LengthSquared), 0.0, 1.0);
		const double Exit = FMath::Clamp((-Linear + Root) / (2.0 * LengthSquared), 0.0, 1.0);
		if (Exit <= Entry + UE_SMALL_NUMBER) return false;
		if (Entry > UE_SMALL_NUMBER) Out.Kept.Emplace(A, A + Delta * Entry);
		if (Exit < 1.0 - UE_SMALL_NUMBER) Out.Kept.Emplace(A + Delta * Exit, B);
		return true;
	}

	void DrawSegment(UCanvas& Canvas, const FMD_GlassDrawingSegment& Segment,
		const UMD_GlassDrawingProfile& Profile, const FVector2D& CanvasSize, const FVector2D& PhysicalSize)
	{
		const FVector2D PixelsPerCm(CanvasSize.X / PhysicalSize.X, CanvasSize.Y / PhysicalSize.Y);
		const FVector2D StartUV = Segment.GetStart();
		const FVector2D EndUV = Segment.GetEnd();
		const FVector2D A(StartUV.X * PhysicalSize.X, StartUV.Y * PhysicalSize.Y);
		const FVector2D B(EndUV.X * PhysicalSize.X, EndUV.Y * PhysicalSize.Y);
		const FLinearColor InkColor = Profile.GetInkColor(Segment.ColorIndex);
		const FVector2D Direction = (B - A).GetSafeNormal();
		const FVector2D HalfWidth(-Direction.Y * Profile.InkWidthCm * 0.5, Direction.X * Profile.InkWidthCm * 0.5);
		auto ToPixels = [&PixelsPerCm](const FVector2D& Point)
		{
			return FVector2D(Point.X * PixelsPerCm.X, Point.Y * PixelsPerCm.Y);
		};
		if (!Direction.IsNearlyZero())
		{
			TArray<FCanvasUVTri> Triangles;
			Triangles.SetNum(2);
			Triangles[0].V0_Pos = ToPixels(A + HalfWidth);
			Triangles[0].V1_Pos = ToPixels(B + HalfWidth);
			Triangles[0].V2_Pos = ToPixels(B - HalfWidth);
			Triangles[1].V0_Pos = ToPixels(A + HalfWidth);
			Triangles[1].V1_Pos = ToPixels(B - HalfWidth);
			Triangles[1].V2_Pos = ToPixels(A - HalfWidth);
			for (FCanvasUVTri& Triangle : Triangles)
			{
				Triangle.V0_Color = Triangle.V1_Color = Triangle.V2_Color = InkColor;
			}
			Canvas.K2_DrawTriangle(nullptr, MoveTemp(Triangles));
		}
		// Physical circular end caps are elliptical in pixels when the texture aspect ratio differs.
		const FVector2D Radius = PixelsPerCm * (Profile.InkWidthCm * 0.5);
		Canvas.K2_DrawPolygon(nullptr, ToPixels(A), Radius, 12, InkColor);
		if (A != B) Canvas.K2_DrawPolygon(nullptr, ToPixels(B), Radius, 12, InkColor);
	}
}

FVector2D FMD_GlassDrawingSegment::GetStart() const
{
	return FVector2D(StartX / MDGlassDrawing::CoordinateMax, StartY / MDGlassDrawing::CoordinateMax);
}

FVector2D FMD_GlassDrawingSegment::GetEnd() const
{
	return FVector2D(EndX / MDGlassDrawing::CoordinateMax, EndY / MDGlassDrawing::CoordinateMax);
}

void FMD_GlassDrawingSegment::SetPoints(const FVector2D& Start, const FVector2D& End)
{
	StartX = MDGlassDrawing::Quantize(Start.X);
	StartY = MDGlassDrawing::Quantize(Start.Y);
	EndX = MDGlassDrawing::Quantize(End.X);
	EndY = MDGlassDrawing::Quantize(End.Y);
}

void FMD_GlassDrawingState::PreReplicatedRemove(const TArrayView<int32>& RemovedIndices, int32 FinalSize)
{
	if (Owner.IsValid()) Owner->QueueVisualUpdate(true);
}

void FMD_GlassDrawingState::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize)
{
	if (Owner.IsValid()) Owner->QueueVisualUpdate(false, AddedIndices);
}

void FMD_GlassDrawingState::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize)
{
	if (Owner.IsValid()) Owner->QueueVisualUpdate(true);
}

AMD_GlassDrawingBoard::AMD_GlassDrawingBoard()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(20.0f);
	SetMinNetUpdateFrequency(2.0f);
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("DrawingRoot")));
	Segments.Owner = this;
}

void AMD_GlassDrawingBoard::InitializeSurface(UStaticMeshComponent* Mesh, UMD_GlassDrawingProfile* InProfile)
{
	if (!HasAuthority() || !IsValid(Mesh) || !IsValid(InProfile) || !InProfile->IsValidForDrawing()
		|| Mesh->GetStaticMesh() != InProfile->ApprovedMesh || IsValid(SourceMesh)) return;
	SourceMesh = Mesh;
	Profile = InProfile;
	SetActorTransform(Mesh->GetComponentTransform());
	AttachToComponent(Mesh, FAttachmentTransformRules::KeepWorldTransform);
}

void AMD_GlassDrawingBoard::BeginPlay()
{
	Super::BeginPlay();
	Segments.Owner = this;
	RegisterSurface();
	QueueVisualUpdate(true);
}

void AMD_GlassDrawingBoard::RegisterSurface()
{
	if (!IsValid(SourceMesh) || bSourceEnded) return;
	if (UMD_GlassDrawingSubsystem* Subsystem = GetWorld()->GetSubsystem<UMD_GlassDrawingSubsystem>())
	{
		Subsystem->RegisterBoard(this);
	}
	if (AActor* SourceOwner = SourceMesh->GetOwner(); IsValid(SourceOwner))
	{
		SourceOwner->OnEndPlay.AddUniqueDynamic(this, &ThisClass::HandleSourceEndPlay);
	}
}

void AMD_GlassDrawingBoard::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PaintTimer);
		if (UMD_GlassDrawingSubsystem* Subsystem = World->GetSubsystem<UMD_GlassDrawingSubsystem>())
		{
			Subsystem->UnregisterBoard(this);
		}
	}
	if (IsValid(SourceMesh) && IsValid(SourceMesh->GetOwner()))
	{
		SourceMesh->GetOwner()->OnEndPlay.RemoveDynamic(this, &ThisClass::HandleSourceEndPlay);
	}
	ReleaseVisuals();
	Segments.Owner.Reset();
	Super::EndPlay(EndPlayReason);
}

void AMD_GlassDrawingBoard::HandleSourceEndPlay(AActor* Actor, EEndPlayReason::Type Reason)
{
	bSourceEnded = true;
	bPaintQueued = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PaintTimer);
		if (UMD_GlassDrawingSubsystem* Subsystem = World->GetSubsystem<UMD_GlassDrawingSubsystem>())
		{
			Subsystem->UnregisterBoard(this);
		}
	}
	ReleaseVisuals();
	if (HasAuthority()) Destroy();
}

void AMD_GlassDrawingBoard::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMD_GlassDrawingBoard, SourceMesh);
	DOREPLIFETIME(AMD_GlassDrawingBoard, Profile);
	DOREPLIFETIME(AMD_GlassDrawingBoard, Segments);
	DOREPLIFETIME(AMD_GlassDrawingBoard, Revision);
}

void AMD_GlassDrawingBoard::OnRep_Surface()
{
	RegisterSurface();
	QueueVisualUpdate(true);
}

void AMD_GlassDrawingBoard::OnRep_Revision()
{
	QueueVisualUpdate(true);
}

FVector AMD_GlassDrawingBoard::GetPointWorldPosition(const FVector2D& UV) const
{
	if (!IsValid(SourceMesh) || !IsValid(Profile) || !Profile->IsValidUV(UV)) return FVector::ZeroVector;
	return SourceMesh->GetComponentTransform().TransformPosition(Profile->UVToLocalPoint(UV,
		0.5 * (double(Profile->FrontFaceY) + Profile->BackFaceY)));
}

FVector2D AMD_GlassDrawingBoard::GetPhysicalSize() const
{
	if (!IsValid(SourceMesh) || !IsValid(Profile)) return FVector2D::ZeroVector;
	const FVector2D LocalSize = Profile->LocalMax - Profile->LocalMin;
	const FVector Scale = SourceMesh->GetComponentTransform().GetScale3D().GetAbs();
	return FVector2D(LocalSize.X * Scale.X, LocalSize.Y * Scale.Z);
}

bool AMD_GlassDrawingBoard::CanAuthor(APlayerState* Author, const FVector2D& UV) const
{
	if (!HasAuthority() || bSourceEnded || !IsValid(SourceMesh) || !IsValid(Profile) || !Profile->IsValidForDrawing()
		|| !Profile->IsValidUV(UV) || !IsValid(Author) || Author->GetWorld() != GetWorld()
		|| !IsValid(SourceMesh->GetOwner()) || SourceMesh->GetOwner()->IsHidden()
		|| SourceMesh->GetStaticMesh() != Profile->ApprovedMesh) return false;
	const APawn* Pawn = Author->GetPawn();
	if (!IsValid(Pawn)) return false;
	const FTransform& Transform = SourceMesh->GetComponentTransform();
	const FVector Scale = Transform.GetScale3D().GetAbs();
	if (Transform.ContainsNaN() || Scale.X <= UE_SMALL_NUMBER || Scale.Y <= UE_SMALL_NUMBER || Scale.Z <= UE_SMALL_NUMBER) return false;
	const AMD_Character* Character = Cast<AMD_Character>(Pawn);
	const UMD_GlassDrawingSubsystem* Drawing = GetWorld()->GetSubsystem<UMD_GlassDrawingSubsystem>();
	const AMD_Character* Occupant = Drawing ? Drawing->GetSurfaceOccupant(SourceMesh) : nullptr;
	if (IsValid(Occupant) && Occupant != Character) return false;
	const UMD_GlassDrawingFocusComponent* Focus = Character ? Character->GetGlassDrawingFocusComponent() : nullptr;
	if (IsValid(Focus) && Focus->IsFocusActive()) return Focus->CanAuthorSurface(SourceMesh, UV);
	const UCameraComponent* Camera = Character ? Character->FindComponentByClass<UCameraComponent>() : nullptr;
	const FVector Eye = IsValid(Camera) ? Camera->GetComponentLocation() : Pawn->GetPawnViewLocation();
	if (Eye.ContainsNaN()) return false;
	const FVector Front = Transform.TransformPosition(Profile->UVToLocalPoint(UV, Profile->FrontFaceY));
	const FVector Back = Transform.TransformPosition(Profile->UVToLocalPoint(UV, Profile->BackFaceY));
	const double DistanceSquared = FMath::Min(FVector::DistSquared(Eye, Front), FVector::DistSquared(Eye, Back));
	return DistanceSquared <= FMath::Square(double(Profile->MaxDrawDistanceCm));
}

bool AMD_GlassDrawingBoard::AppendSegment(APlayerState* Author, const FVector2D& Start, const FVector2D& End, uint8 ColorIndex)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_GlassDrawing_Append);
	if (!CanAuthor(Author, Start) || !CanAuthor(Author, End)
		|| !Profile->IsValidColorIndex(ColorIndex)
		|| FVector::Distance(GetPointWorldPosition(Start), GetPointWorldPosition(End)) > Profile->MaxSegmentLengthCm) return false;
	FMD_GlassDrawingSegment NewSegment;
	NewSegment.SetPoints(Start, End);
	NewSegment.AuthorPlayerId = Author->GetPlayerId();
	NewSegment.ColorIndex = ColorIndex;
	if (!Segments.Items.IsEmpty() && Start.Equals(End, UE_SMALL_NUMBER))
	{
		const FMD_GlassDrawingSegment& Last = Segments.Items.Last();
		if (Last.AuthorPlayerId == NewSegment.AuthorPlayerId && Last.ColorIndex == ColorIndex
			&& Last.EndX == NewSegment.EndX && Last.EndY == NewSegment.EndY) return true;
	}
	bool bEvicted = false;
	if (Segments.Items.Num() >= Profile->MaxSegments)
	{
		const int32 Oldest = Profile->bOnlyAuthorMayErase
			? Segments.Items.IndexOfByPredicate([&NewSegment](const FMD_GlassDrawingSegment& Segment)
				{ return Segment.AuthorPlayerId == NewSegment.AuthorPlayerId; }) : 0;
		if (Oldest == INDEX_NONE) return false;
		Segments.Items.RemoveAt(Oldest);
		Segments.MarkArrayDirty();
		bEvicted = true;
	}
	NewSegment.PaintOrder = NextPaintOrder++;
	const int32 AddedIndex = Segments.Items.Add(MoveTemp(NewSegment));
	Segments.MarkItemDirty(Segments.Items[AddedIndex]);
	NotifyServerChange(bEvicted, AddedIndex);
	return true;
}

int32 AMD_GlassDrawingBoard::GetSegmentColorCount(uint8 ColorIndex) const
{
	int32 Count = 0;
	for (const FMD_GlassDrawingSegment& Segment : Segments.Items)
	{
		if (Segment.ColorIndex == ColorIndex) ++Count;
	}
	return Count;
}

bool AMD_GlassDrawingBoard::EraseAt(APlayerState* Author, const FVector2D& UV)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_GlassDrawing_Erase);
	if (!CanAuthor(Author, UV)) return false;
	const FVector2D Size = GetPhysicalSize();
	auto ToCentimetres = [&Size](const FVector2D& Point) { return FVector2D(Point.X * Size.X, Point.Y * Size.Y); };
	const FVector2D Center = ToCentimetres(UV);
	TArray<MDGlassDrawing::FClip> Changes;
	int32 ResultingCount = Segments.Items.Num();
	for (int32 Index = 0; Index < Segments.Items.Num(); ++Index)
	{
		const FMD_GlassDrawingSegment& Segment = Segments.Items[Index];
		if (Profile->bOnlyAuthorMayErase && Segment.AuthorPlayerId != Author->GetPlayerId()) continue;
		MDGlassDrawing::FClip Clip;
		Clip.Index = Index;
		if (MDGlassDrawing::ClipAgainstDisk(ToCentimetres(Segment.GetStart()),
			ToCentimetres(Segment.GetEnd()), Center, Profile->EraserRadiusCm + Profile->InkWidthCm * 0.5, Clip))
		{
			ResultingCount += Clip.Kept.Num() - 1;
			Changes.Add(MoveTemp(Clip));
		}
	}
	if (Changes.IsEmpty()) return false;
	// Fragmentation cannot make a full board impossible to erase. Remove only already-affected ink,
	// with the same author filter, in stable oldest-first order until the transaction fits.
	for (MDGlassDrawing::FClip& Change : Changes)
	{
		if (ResultingCount <= Profile->MaxSegments) break;
		if (Change.Kept.Num() == 2)
		{
			ResultingCount -= 2;
			Change.Kept.Reset();
		}
	}

	// Commit only after the complete bounded clipping transaction has passed capacity checks.
	for (int32 ChangeIndex = Changes.Num() - 1; ChangeIndex >= 0; --ChangeIndex)
	{
		const MDGlassDrawing::FClip& Change = Changes[ChangeIndex];
		if (Change.Kept.IsEmpty())
		{
			Segments.Items.RemoveAt(Change.Index);
			Segments.MarkArrayDirty();
			continue;
		}
		auto ToUV = [&Size](const FVector2D& Point) { return FVector2D(Point.X / Size.X, Point.Y / Size.Y); };
		FMD_GlassDrawingSegment& Original = Segments.Items[Change.Index];
		const int32 AuthorId = Original.AuthorPlayerId;
		const uint8 ColorIndex = Original.ColorIndex;
		const uint64 PaintOrder = Original.PaintOrder;
		Original.SetPoints(ToUV(Change.Kept[0].Key), ToUV(Change.Kept[0].Value));
		Segments.MarkItemDirty(Original);
		if (Change.Kept.Num() == 2)
		{
			FMD_GlassDrawingSegment& Split = Segments.Items.AddDefaulted_GetRef();
			Split.SetPoints(ToUV(Change.Kept[1].Key), ToUV(Change.Kept[1].Value));
			Split.AuthorPlayerId = AuthorId;
			Split.ColorIndex = ColorIndex;
			Split.PaintOrder = PaintOrder;
			Segments.MarkItemDirty(Split);
		}
	}
	if (++Revision == 0) ++Revision;
	NotifyServerChange(true);
	return true;
}

void AMD_GlassDrawingBoard::ResetDrawing()
{
	if (!HasAuthority()) return;
	Segments.Items.Reset();
	NextPaintOrder = 1;
	Segments.MarkArrayDirty();
	if (++Revision == 0) ++Revision;
	NotifyServerChange(true);
}

void AMD_GlassDrawingBoard::NotifyServerChange(bool bRebuild, int32 AddedIndex)
{
	if (AddedIndex != INDEX_NONE)
	{
		QueueVisualUpdate(bRebuild, MakeArrayView(&AddedIndex, 1));
	}
	else QueueVisualUpdate(bRebuild);
	ForceNetUpdate();
}

void AMD_GlassDrawingBoard::QueueVisualUpdate(bool bRebuild, const TArrayView<int32>& AddedIndices)
{
	if (!GetWorld() || bSourceEnded || GetNetMode() == NM_DedicatedServer || GUsingNullRHI || IsActorBeingDestroyed()) return;
	bRebuildPending |= bRebuild;
	if (!bRebuildPending)
	{
		for (int32 Index : AddedIndices)
		{
			if (Segments.Items.IsValidIndex(Index)) PendingPaint.Add(Segments.Items[Index]);
		}
	}
	if (bPaintQueued) return;
	bPaintQueued = true;
	PaintTimer = GetWorld()->GetTimerManager().SetTimerForNextTick(this, &ThisClass::FlushVisuals);
}

bool AMD_GlassDrawingBoard::EnsureVisuals()
{
	if (bSourceEnded || !IsValid(SourceMesh) || !IsValid(Profile) || !Profile->IsValidForDrawing()
		|| GetNetMode() == NM_DedicatedServer || GUsingNullRHI) return false;
	const FTransform& SourceTransform = SourceMesh->GetComponentTransform();
	const FVector Scale = SourceTransform.GetScale3D().GetAbs();
	if (SourceTransform.ContainsNaN() || Scale.X <= UE_SMALL_NUMBER || Scale.Y <= UE_SMALL_NUMBER || Scale.Z <= UE_SMALL_NUMBER)
	{
		ReleaseVisuals();
		return false;
	}
	if (IsValid(InkTexture) && IsValid(FrontInk) && IsValid(BackInk)) return true;
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_GlassDrawing_CreateVisuals);
	InkTexture = UKismetRenderingLibrary::CreateRenderTarget2D(this, Profile->TextureWidth, Profile->TextureHeight,
		RTF_RGBA8, FLinearColor::Transparent, false, false);
	if (!IsValid(InkTexture)) return false;
	InkTexture->AddressX = TA_Clamp;
	InkTexture->AddressY = TA_Clamp;
	FrontInkMaterial = UMaterialInstanceDynamic::Create(Profile->InkMaterial, this);
	BackInkMaterial = UMaterialInstanceDynamic::Create(Profile->InkMaterial, this);
	if (!IsValid(FrontInkMaterial) || !IsValid(BackInkMaterial))
	{
		ReleaseVisuals();
		return false;
	}
	for (UMaterialInstanceDynamic* Material : { FrontInkMaterial.Get(), BackInkMaterial.Get() })
	{
		Material->SetTextureParameterValue(TEXT("InkMask"), InkTexture);
		Material->SetScalarParameterValue(TEXT("InkGlow"), Profile->GlowStrength);
	}
	// Front plane needs opposite winding. Compensating V keeps the same physical X/Z ink on both sides.
	// Engine Plane V runs along +Y. With the outward rotations below, only the back
	// face needs a vertical flip to keep profile V=0 at the glass's upper edge.
	FrontInkMaterial->SetScalarParameterValue(TEXT("InkFlipV"), 0.0f);
	BackInkMaterial->SetScalarParameterValue(TEXT("InkFlipV"), 1.0f);
	const FVector2D Size = Profile->LocalMax - Profile->LocalMin;
	const FVector2D Center = (Profile->LocalMin + Profile->LocalMax) * 0.5;
	const double Offset = Profile->SurfaceOffsetCm / Scale.Y;
	// Quarter-turn swaps plane Y/Z, while UE composes scale componentwise. Compensate
	// magnitudes only; negative-scale matrix composition already preserves the reflected axes.
	const FVector PlaneScale(Size.X / 100.0, Size.Y / 100.0 * Scale.Z / Scale.Y, Scale.Y / Scale.Z);
	auto CreatePlane = [&](const TCHAR* Name, float Y, float Roll, UMaterialInstanceDynamic* Material)
	{
		UStaticMeshComponent* Plane = NewObject<UStaticMeshComponent>(this, Name);
		Plane->SetMobility(EComponentMobility::Movable);
		Plane->SetStaticMesh(Profile->InkPlaneMesh);
		Plane->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Plane->SetGenerateOverlapEvents(false);
		Plane->SetCanEverAffectNavigation(false);
		Plane->SetCastShadow(false);
		Plane->SetReceivesDecals(false);
		Plane->SetRenderCustomDepth(false);
		Plane->SetMaterial(0, Material);
		Plane->SetupAttachment(SourceMesh);
		Plane->SetRelativeLocation(FVector(Center.X, Y, Center.Y));
		Plane->SetRelativeRotation(FRotator(0.0, 0.0, Roll));
		Plane->SetRelativeScale3D(PlaneScale);
		AddInstanceComponent(Plane);
		Plane->RegisterComponent();
		return Plane;
	};
	FrontInk = CreatePlane(TEXT("GlassInkFront"), Profile->FrontFaceY + Offset, 90.0f, FrontInkMaterial);
	BackInk = CreatePlane(TEXT("GlassInkBack"), Profile->BackFaceY - Offset, -90.0f, BackInkMaterial);
	bRebuildPending = true;
	return true;
}

void AMD_GlassDrawingBoard::FlushVisuals()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(MD_GlassDrawing_Flush);
	bPaintQueued = false;
	if (!EnsureVisuals()) return;
	// FastArray indices may differ between peers. An older late addition needs a full replay
	// rather than painting over newer colors already present in the incremental texture.
	if (!bRebuildPending)
	{
		for (const FMD_GlassDrawingSegment& Segment : PendingPaint)
		{
			if (Segment.PaintOrder <= LastPaintedOrder)
			{
				bRebuildPending = true;
				break;
			}
		}
	}
	if (bRebuildPending)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, InkTexture, FLinearColor::Transparent);
		LastPaintedOrder = 0;
	}
	const TArray<FMD_GlassDrawingSegment>& ToDraw = bRebuildPending ? Segments.Items : PendingPaint;
	if (!ToDraw.IsEmpty())
	{
		TArray<const FMD_GlassDrawingSegment*, TInlineAllocator<32>> OrderedSegments;
		OrderedSegments.Reserve(ToDraw.Num());
		for (const FMD_GlassDrawingSegment& Segment : ToDraw) OrderedSegments.Add(&Segment);
		Algo::SortBy(OrderedSegments, [](const FMD_GlassDrawingSegment* Segment) { return Segment->PaintOrder; });
		UCanvas* Canvas = nullptr;
		FVector2D CanvasSize;
		FDrawToRenderTargetContext Context;
		UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, InkTexture, Canvas, CanvasSize, Context);
		if (Canvas)
		{
			const FVector2D PhysicalSize = GetPhysicalSize();
			for (const FMD_GlassDrawingSegment* Segment : OrderedSegments)
			{
				MDGlassDrawing::DrawSegment(*Canvas, *Segment, *Profile, CanvasSize, PhysicalSize);
				LastPaintedOrder = FMath::Max(LastPaintedOrder, Segment->PaintOrder);
			}
		}
		UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);
	}
	PendingPaint.Reset();
	bRebuildPending = false;
}

void AMD_GlassDrawingBoard::ReleaseVisuals()
{
	PendingPaint.Empty();
	if (IsValid(FrontInk)) FrontInk->DestroyComponent();
	if (IsValid(BackInk)) BackInk->DestroyComponent();
	FrontInk = nullptr;
	BackInk = nullptr;
	FrontInkMaterial = nullptr;
	BackInkMaterial = nullptr;
	if (IsValid(InkTexture)) InkTexture->ReleaseResource();
	InkTexture = nullptr;
	LastPaintedOrder = 0;
	bRebuildPending = true;
}
