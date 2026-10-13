#include "World/Wall/MD_GlassDrawingWall.h"

#include "Components/StaticMeshComponent.h"
#include "Data/Drawing/MD_GlassDrawingProfile.h"
#include "Character/MD_Character.h"
#include "UObject/ConstructorHelpers.h"

AMD_GlassDrawingWall::AMD_GlassDrawingWall()
{
	static ConstructorHelpers::FObjectFinder<UMD_GlassDrawingProfile> DefaultProfile(
		TEXT("/Game/Data/Drawing/DA_GlassDrawingProfile.DA_GlassDrawingProfile"));
	DeskMesh->SetStaticMesh(DefaultProfile.Succeeded() ? DefaultProfile.Object->ApprovedMesh.Get() : nullptr);
	DisplayName = NSLOCTEXT("MDGlassDrawingWall", "DefaultName", "그림용 유리벽");
	ConfigureOwnGlass();
	RefreshPenDisplay();
}

void AMD_GlassDrawingWall::ConfigureOwnGlass()
{
	// Do not inherit a CDO/self reference or allow another pane to replace this wall's target.
	TargetGlassActor = this;
	TargetGlassComponent = FComponentReference();
	TargetGlassComponent.ComponentProperty = GET_MEMBER_NAME_CHECKED(AMD_GlassDrawingDesk, DeskMesh);
}

void AMD_GlassDrawingWall::OnConstruction(const FTransform& Transform)
{
	ConfigureOwnGlass();
	Super::OnConstruction(Transform);
}

void AMD_GlassDrawingWall::BeginPlay()
{
	ConfigureOwnGlass();
	Super::BeginPlay();
	RefreshPenDisplay();
}

void AMD_GlassDrawingWall::RefreshPenDisplay()
{
	if (!IsValid(PenDisplayMesh)) return;
	PenDisplayMesh->SetStaticMesh(nullptr);
	PenDisplayMesh->SetVisibility(false);
	PenDisplayMesh->SetHiddenInGame(true);
}

FText AMD_GlassDrawingWall::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	if (!HasValidConfiguration())
		return NSLOCTEXT("MDGlassDrawingWall", "Unconfigured", "유리 그리기 설정 없음");
	if (IsValid(GetOccupant()) && GetOccupant() != Interactor)
		return NSLOCTEXT("MDGlassDrawingWall", "Busy", "다른 대원이 사용 중");
	return GetOccupant() == Interactor
		? NSLOCTEXT("MDGlassDrawingWall", "Leave", "[E] 유리 그리기에서 나가기")
		: NSLOCTEXT("MDGlassDrawingWall", "Enter", "[E] 유리에 그리기");
}
