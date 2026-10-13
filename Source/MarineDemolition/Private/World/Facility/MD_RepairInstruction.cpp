#include "World/Facility/MD_RepairInstruction.h"
#include "Audio/MD_GameplayAudioFeedback.h"
#include "World/Wall/MD_PowerConduit.h"
#include "World/Door/MD_DoorFaultComponent.h"
#include "Component/Facility/MD_FacilityRepairComponent.h"
#include "Component/Interaction/MD_InteractionComponent.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Components/SpotLightComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TriggerBox.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "Gameplay/Collision/MD_CollisionChannels.h"
#include "Gameplay/Interaction/MD_InteractionPresentationLibrary.h"
#include "Gameplay/Interaction/MD_InteractionSettings.h"
#include "Gameplay/Settings/MD_ControlSettingCategory.h"
#include "Gameplay/Settings/MD_SettingsManager.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UnrealType.h"

namespace { void FitInstructionMesh(UStaticMeshComponent* Component, const FVector& Center, const FVector& Size); }

AMD_RepairInstructionController::AMD_RepairInstructionController()
{
	bReplicates = true; bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.TickInterval = 0.1f;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	Fault = CreateDefaultSubobject<UMD_FacilityFaultComponent>(TEXT("Fault"));
	Fault->Kind = EMD_FacilityKind::Breaker; Fault->bRandomFaultEligible = false;
	BreakerBody=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BreakerBody")); BreakerBody->SetupAttachment(RootComponent); BreakerBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BreakerInteractionBounds=CreateDefaultSubobject<UBoxComponent>(TEXT("BreakerInteractionBounds"));
	BreakerInteractionBounds->SetupAttachment(RootComponent); BreakerInteractionBounds->SetBoxExtent(FVector(10,64,66));
	BreakerInteractionBounds->SetRelativeLocation(FVector(9,0,0));
	BreakerInteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly); BreakerInteractionBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	BreakerInteractionBounds->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); BreakerInteractionBounds->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace,ECR_Block);
	BreakerInteractionBounds->SetGenerateOverlapEvents(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> KnobMesh(TEXT("/Game/Assets/X7/Interior/Objects/RepairInstruction/SM_BreakerKnob.SM_BreakerKnob"));
	for (int32 Column=0; Column<4; ++Column)
	{
		auto* Bounds=CreateDefaultSubobject<UBoxComponent>(*FString::Printf(TEXT("KnobBounds_%d"),Column));
		Bounds->SetupAttachment(RootComponent); Bounds->SetRelativeLocation(FVector(9,45-Column*30,0)); Bounds->SetBoxExtent(FVector(9,14,66));
		Bounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Bounds->SetCollisionResponseToAllChannels(ECR_Ignore);
		Bounds->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block); Bounds->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace,ECR_Block);
		Bounds->SetGenerateOverlapEvents(false); KnobBounds.Add(Bounds);
		auto* Mesh=CreateDefaultSubobject<UStaticMeshComponent>(*FString::Printf(TEXT("KnobMesh_%d"),Column));
		Mesh->SetupAttachment(RootComponent); Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetRelativeRotation(FRotator(0,-90,0));
		if(KnobMesh.Succeeded())
		{
			Mesh->SetStaticMesh(KnobMesh.Object);
			// Initial fit only. Subsequent construction/state updates preserve artist edits.
			FitInstructionMesh(Mesh,FVector::ZeroVector,FVector(1.8,4.25,5.68));
		}
		KnobMeshes.Add(Mesh);
	}
}

void AMD_RepairInstructionController::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform); RefreshKnobs();
}

void AMD_RepairInstructionController::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMD_RepairInstructionController, State);
	DOREPLIFETIME(AMD_RepairInstructionController, Guide); DOREPLIFETIME(AMD_RepairInstructionController, Restart);
	DOREPLIFETIME(AMD_RepairInstructionController, Knobs); DOREPLIFETIME(AMD_RepairInstructionController, InstructionDoor);
	DOREPLIFETIME(AMD_RepairInstructionController, OperatingPawn); DOREPLIFETIME(AMD_RepairInstructionController, OperationId);
}

bool AMD_RepairInstructionController::HasValidConfiguration() const
{
	if (!IsValid(Guide) || !IsValid(Restart) || Guide == Restart
		|| Guide->Controller != this || Restart->Controller != this
		|| Guide->ControlKind != EMD_InstructionControlKind::Guide || Restart->ControlKind != EMD_InstructionControlKind::Restart
		|| Guide->GetWorld() != GetWorld() || Restart->GetWorld() != GetWorld()
		|| !IsValid(InstructionRoom) || !IsValid(InstructionDoor)
		|| InstructionDoor->GetWorld() != GetWorld() || InstructionRoom->GetWorld() != GetWorld()) return false;
	const UFunction* Apply = InstructionDoor->FindFunction(TEXT("OnRep_bDoorOpen"));
	if (!Apply || Apply->NumParms != 0 || !FindFProperty<FBoolProperty>(InstructionDoor->GetClass(),TEXT("bDoorOpen"))) return false;
	if (bUseIntegratedKnobs)
	{
		if (KnobBounds.Num()!=4 || KnobMeshes.Num()!=4) return false;
		for(int32 I=0;I<4;++I)
			if (!IsValid(KnobBounds[I]) || KnobBounds[I]->GetOwner()!=this || !IsValid(KnobMeshes[I]) || !KnobMeshes[I]->GetStaticMesh()) return false;
	}
	else
	{
		if(Knobs.Num()!=4) return false;
		TSet<const AMD_RepairInstructionControl*> Unique;
		for (int32 I=0; I<4; ++I)
		{
			const AMD_RepairInstructionControl* Knob=Knobs[I];
			if (!IsValid(Knob) || Knob->Controller!=this || Knob->ControlKind!=EMD_InstructionControlKind::Knob
				|| Knob->Column!=I || Knob->GetWorld()!=GetWorld() || Unique.Contains(Knob)) return false;
			Unique.Add(Knob);
		}
	}
	const UBoxComponent* Box = Cast<UBoxComponent>(InstructionRoom->GetCollisionComponent());
	return Box && Box->GetUnscaledBoxExtent().GetMin() > 0.f && Box->GetComponentScale().GetAbs().GetMin() > SMALL_NUMBER;
}

TArray<int32> AMD_RepairInstructionController::GenerateDifferentAnswer(const TArray<int32>& Previous, FRandomStream& Rng)
{
	TArray<int32> Result; for (int32 I=0; I<4; ++I) Result.Add(Rng.RandRange(0,4));
	// Never start solved at the top row, and never reuse the previous restart answer.
	if (Result == Previous || Result == TArray<int32>({0,0,0,0})) Result[0] = (Result[0]+1)%5;
	if (Result == Previous) Result[1] = (Result[1]+1)%5;
	return Result;
}

void AMD_RepairInstructionController::BeginPlay()
{
	Super::BeginPlay(); Random.Initialize(FMath::Rand());
	Fault->OnStateChanged.AddUniqueDynamic(this,&ThisClass::HandleOwnFault);
	if (HasAuthority())
	{
		if(IsValid(InstructionDoor) && InstructionDoor->GetWorld()==GetWorld())
		{ InstructionDoor->SetReplicates(true); InstructionDoor->FlushNetDormancy(); InstructionDoor->ForceNetUpdate(); }
		// Components can enter an initial fault before the actor's BeginPlay binds its delegate.
		if (Fault->IsFaultActive()) HandleOwnFault(Fault,FMD_FacilitySnapshot(),Fault->GetSnapshot());
		for (AMD_PowerConduit* Wire : PowerConduits)
			if (IsValid(Wire) && Wire->GetWorld()==GetWorld() && Wire->FacilityRepair)
				Wire->FacilityRepair->OnStateChanged.AddUniqueDynamic(this,&ThisClass::HandlePowerFault);
		TrackBrokenPowerSources();
		if (!SourceIncidents.IsEmpty()) StartRepairIncident();
	}
	OnRep_State();
}

void AMD_RepairInstructionController::EndPlay(const EEndPlayReason::Type Reason)
{
	for (AMD_PowerConduit* Wire : PowerConduits)
		if (IsValid(Wire) && Wire->FacilityRepair) Wire->FacilityRepair->OnStateChanged.RemoveDynamic(this,&ThisClass::HandlePowerFault);
	Fault->OnStateChanged.RemoveDynamic(this,&ThisClass::HandleOwnFault);
	ReleaseDoor(); SourceIncidents.Empty();
	Super::EndPlay(Reason);
}

void AMD_RepairInstructionController::TrackBrokenPowerSources()
{
	for (AMD_PowerConduit* Wire : PowerConduits)
		if (IsValid(Wire) && Wire->GetWorld()==GetWorld() && Wire->FacilityRepair && Wire->FacilityRepair->IsFaultActive())
			SourceIncidents.Add(Wire->FacilityRepair.Get(),Wire->FacilityRepair->GetSnapshot().FaultId);
}

void AMD_RepairInstructionController::HandlePowerFault(UMD_FacilityFaultComponent* Source, const FMD_FacilitySnapshot&, const FMD_FacilitySnapshot& Current)
{
	if (!HasAuthority() || bCompleting || !Source || !PowerConduits.Contains(Cast<AMD_PowerConduit>(Source->GetOwner()))) return;
	if (Current.IsFaultActive())
	{
		SourceIncidents.Add(Source,Current.FaultId);
		if (!State.bActive) StartRepairIncident();
	}
	else
	{
		SourceIncidents.Remove(Source);
		// An external tape repair stays valid. Do not leave readers locked after all linked power is restored.
		if (State.bActive && SourceIncidents.IsEmpty()) Fault->CompleteRepairFromValidatedSource(Fault->GetSnapshot().FaultId);
	}
}

bool AMD_RepairInstructionController::StartRepairIncident()
{
	if (!HasAuthority() || State.bActive || !HasValidConfiguration()) return false;
	return Fault->TryBreak();
}

void AMD_RepairInstructionController::HandleOwnFault(UMD_FacilityFaultComponent*, const FMD_FacilitySnapshot& Previous, const FMD_FacilitySnapshot& Current)
{
	if (!HasAuthority() || Previous.IsFaultActive()==Current.IsFaultActive()) return;
	State.bActive = Current.IsFaultActive(); State.bDoorLocked = false; State.bGuideRead = false;
	State.RestartFlashUntil=0;
	if (State.bActive)
	{
		const auto* Locked=IsValid(InstructionDoor)?FindFProperty<FBoolProperty>(InstructionDoor->GetClass(),TEXT("bLocked")):nullptr;
		State.bDoorPreviouslyLocked=Locked && Locked->GetPropertyValue_InContainer(InstructionDoor);
		State.Revision = State.Revision == MAX_int32 ? 1 : State.Revision+1;
		State.TargetRows = GenerateDifferentAnswer(State.TargetRows,Random); State.CurrentRows = {0,0,0,0};
		TrackBrokenPowerSources();
	}
	else SourceIncidents.Empty();
	PublishState();
}

int32 AMD_RepairInstructionController::GetParticipantCount() const
{
	int32 Count=0;
	if (const AGameStateBase* GS=GetWorld()?GetWorld()->GetGameState():nullptr)
		for (const APlayerState* PS:GS->PlayerArray)
			if (IsValid(PS) && !PS->IsInactive() && !PS->IsSpectator() && !PS->IsOnlyASpectator()) ++Count;
	return FMath::Clamp(Count,1,4);
}

bool AMD_RepairInstructionController::IsInInstructionRoom(const APawn* Pawn) const
{
	const UBoxComponent* Box=IsValid(InstructionRoom)?Cast<UBoxComponent>(InstructionRoom->GetCollisionComponent()):nullptr;
	if (!IsValid(Pawn) || !Box) return false;
	const FVector P=Box->GetComponentTransform().InverseTransformPosition(Pawn->GetActorLocation());
	const FVector E=Box->GetUnscaledBoxExtent();
	return !P.ContainsNaN() && FMath::Abs(P.X)<=E.X && FMath::Abs(P.Y)<=E.Y && FMath::Abs(P.Z)<=E.Z;
}

bool AMD_RepairInstructionController::CanApplyInput(const APawn* Pawn, int32 ExpectedRevision) const
{
	const auto* Interaction=IsValid(Pawn)?Pawn->FindComponentByClass<UMD_InteractionComponent>():nullptr;
	const auto* GS=GetWorld()?GetWorld()->GetGameState():nullptr; const auto* PS=Pawn?Pawn->GetPlayerState():nullptr;
	if (!HasAuthority() || !State.bActive || !HasValidConfiguration() || ExpectedRevision!=State.Revision
		|| !IsValid(Pawn) || Pawn->GetWorld()!=GetWorld()
		|| !Pawn->GetController() || !Interaction || !Interaction->CanOwnerInteract() || Interaction->IsHolding()
		|| !IsValid(PS) || !GS || !GS->PlayerArray.Contains(PS) || PS->IsInactive() || PS->IsSpectator() || PS->IsOnlyASpectator()) return false;
	return true;
}

bool AMD_RepairInstructionController::ApplyControl(AMD_RepairInstructionControl* Control, APawn* Pawn, int32 ExpectedRevision)
{
	if(!CanApplyInput(Pawn,ExpectedRevision) || !IsValid(Control) || Control->Controller!=this) return false;
	if (Control==Guide || Control==Restart)
	{
		if (!IsInInstructionRoom(Pawn)) return false;
		if (Control==Guide) { State.bGuideRead=true; State.bDoorLocked=GetParticipantCount()>1; }
		else
		{
			State.TargetRows=GenerateDifferentAnswer(State.TargetRows,Random); State.CurrentRows={0,0,0,0};
			State.Revision=State.Revision==MAX_int32?1:State.Revision+1;
			State.bGuideRead=false; State.bDoorLocked=false;
			State.RestartFlashUntil=GetWorld()->GetGameState()->GetServerWorldTimeSeconds()+FMath::Clamp(Guide->RestartFlashSeconds,.1f,3.f);
		}
	}
	else
	{
		const int32 Column=Control->Column;
		if (bUseIntegratedKnobs || !Knobs.IsValidIndex(Column) || Knobs[Column]!=Control) return false;
		return AdvanceKnob(Column);
	}
	PublishState(); return true;
}

bool AMD_RepairInstructionController::AdvanceKnob(int32 Column)
{
	if(!State.bGuideRead || !State.CurrentRows.IsValidIndex(Column)) return false;
	State.CurrentRows[Column]=(State.CurrentRows[Column]+1)%5;
	if(State.CurrentRows==State.TargetRows) FinishRepair(); else PublishState();
	return true;
}

bool AMD_RepairInstructionController::ApplyKnob(APawn* Pawn, int32 Column, int32 ExpectedRevision)
{
	// Integrated breakers now accept authenticated drag releases, not the old E-to-cycle path.
	return false;
}

int32 AMD_RepairInstructionController::GetAimedKnobColumn(const APawn* Pawn) const
{
	const auto* PC=IsValid(Pawn)?Pawn->GetController():nullptr;
	const auto* Interaction=IsValid(Pawn)?Pawn->FindComponentByClass<UMD_InteractionComponent>():nullptr;
	if(!bUseIntegratedKnobs || !PC || !Interaction || Pawn->GetWorld()!=GetWorld()) return INDEX_NONE;
	FVector Eye; FRotator Rotation; PC->GetPlayerViewPoint(Eye,Rotation);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(IntegratedBreaker),false,Pawn);
	FHitResult Hit;
	if(!GetWorld()->LineTraceSingleByChannel(Hit,Eye,Eye+Rotation.Vector()*Interaction->GetInteractTraceDistance(),MDCollisionChannels::InteractTrace,Query) || Hit.GetActor()!=this) return INDEX_NONE;
	for(int32 Column=0;Column<KnobBounds.Num();++Column) if(Hit.GetComponent()==KnobBounds[Column]) return Column;
	if(Hit.GetComponent()==BreakerInteractionBounds)
	{
		const FVector Point=GetActorTransform().InverseTransformPosition(Hit.ImpactPoint);
		int32 Closest=0; double Distance=MAX_dbl;
		for(int32 Column=0;Column<4;++Column)
		{
			const double D=FMath::Abs(Point.Y-GetKnobSlotCenter(Column,2).Y);
			if(D<Distance) { Closest=Column; Distance=D; }
		}
		return Closest;
	}
	return INDEX_NONE;
}

FVector AMD_RepairInstructionController::GetPanelNormal() const
{
	return BreakerBody?BreakerBody->GetComponentTransform().TransformVectorNoScale(FVector::YAxisVector).GetSafeNormal():GetActorForwardVector();
}

bool AMD_RepairInstructionController::CanContinueOperation(const APawn* Pawn, int32 ExpectedRevision) const
{
	if(!CanApplyInput(Pawn,ExpectedRevision) || !State.bGuideRead || !bUseIntegratedKnobs) return false;
	const auto* Interaction=Pawn->FindComponentByClass<UMD_InteractionComponent>();
	FVector Eye; FRotator Rotation; Pawn->GetActorEyesViewPoint(Eye,Rotation);
	const FVector Target=BreakerInteractionBounds->GetComponentLocation();
	if(FVector::Dist(Eye,Target)>Interaction->GetInteractTraceDistance()) return false;
	FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(BreakerOperationReach),false,Pawn);
	return GetWorld()->LineTraceSingleByChannel(Hit,Eye,Target,MDCollisionChannels::InteractTrace,Query) && Hit.GetActor()==this;
}

int32 AMD_RepairInstructionController::BeginOperation(APawn* Pawn, int32 ExpectedRevision)
{
	if(IsValid(OperatingPawn) || !CanContinueOperation(Pawn,ExpectedRevision) || GetAimedKnobColumn(Pawn)==INDEX_NONE) return 0;
	OperatingPawn=Pawn; NextOperationId=NextOperationId==MAX_int32?1:NextOperationId+1; OperationId=NextOperationId;
	FlushNetDormancy(); ForceNetUpdate(); return OperationId;
}

void AMD_RepairInstructionController::EndOperation(APawn* Pawn, int32 ExpectedOperationId)
{
	if(!HasAuthority() || OperatingPawn!=Pawn || OperationId!=ExpectedOperationId) return;
	OperatingPawn=nullptr; OperationId=0; FlushNetDormancy(); ForceNetUpdate();
}

bool AMD_RepairInstructionController::ApplyKnobSlot(APawn* Pawn, int32 Column, int32 Row, int32 ExpectedRevision, int32 ExpectedOperationId)
{
	if(!IsValid(Pawn) || OperatingPawn!=Pawn || OperationId==0 || OperationId!=ExpectedOperationId
		|| !State.CurrentRows.IsValidIndex(Column) || Column>=4 || Row<0 || Row>=5 || !CanContinueOperation(Pawn,ExpectedRevision)) return false;
	if(State.CurrentRows[Column]==Row) return true;
	State.CurrentRows[Column]=Row;
	if(State.CurrentRows==State.TargetRows) FinishRepair(); else PublishState();
	return true;
}

int32 AMD_RepairInstructionController::FindNearestKnobSlot(int32 Column, const FVector& RootLocalPoint) const
{
	if(Column<0 || Column>=4 || RootLocalPoint.ContainsNaN()) return INDEX_NONE;
	int32 Closest=0; double Distance=MAX_dbl;
	for(int32 Row=0;Row<5;++Row)
	{
		const double D=FVector::DistSquared(RootLocalPoint,GetKnobSlotCenter(Column,Row));
		if(D<Distance) { Closest=Row; Distance=D; }
	}
	return Closest;
}

void AMD_RepairInstructionController::PreviewKnobAt(int32 Column, const FVector& RootLocalSurface)
{
	if(!KnobMeshes.IsValidIndex(Column) || !BreakerBody || RootLocalSurface.ContainsNaN()) return;
	auto* Knob=KnobMeshes[Column].Get(); if(!Knob || !Knob->GetStaticMesh()) return;
	const FVector Normal=BreakerBody->GetRelativeTransform().GetRotation().RotateVector(FVector::YAxisVector);
	const FTransform Shape(Knob->GetRelativeRotation(),FVector::ZeroVector,Knob->GetRelativeScale3D());
	const double HalfDepth=FVector::DotProduct(Knob->CalcBounds(Shape).BoxExtent,Normal.GetAbs());
	Knob->SetRelativeLocation(RootLocalSurface+Normal*HalfDepth-Shape.TransformPosition(Knob->GetStaticMesh()->GetBounds().Origin));
}

FVector AMD_RepairInstructionController::GetKnobSlotCenter(int32 Column, int32 Row) const
{
	if (!BreakerBody || Column<0 || Column>=4 || Row<0 || Row>=5) return FVector::ZeroVector;
	// Atlas hole centers projected onto the imported base mesh (centimeters).
	// The UV islands run sideways: the first texture hole is physically at the bottom.
	static constexpr double Columns[] = {-21.72,-7.305,7.11,21.525};
	static constexpr double Rows[] = {48.114,41.056,34.069,26.94,20.163};
	return BreakerBody->GetRelativeTransform().TransformPosition(FVector(Columns[Column],8.25,Rows[Row]));
}

void AMD_RepairInstructionController::RefreshKnobs()
{
	if(BreakerBody && BreakerBody->GetStaticMesh() && BreakerInteractionBounds)
	{
		const auto Bounds=BreakerBody->CalcBounds(BreakerBody->GetRelativeTransform());
		const FVector Normal=BreakerBody->GetRelativeTransform().GetRotation().RotateVector(FVector::YAxisVector);
		BreakerInteractionBounds->SetRelativeLocation(Bounds.Origin+Normal*9);
		BreakerInteractionBounds->SetBoxExtent(Bounds.BoxExtent+Normal.GetAbs()*9+FVector(1));
	}
	for(int32 Column=0;Column<KnobMeshes.Num();++Column)
	{
		const int32 Row=State.CurrentRows.IsValidIndex(Column)?FMath::Clamp(State.CurrentRows[Column],0,4):0;
		KnobMeshes[Column]->SetVisibility(bUseIntegratedKnobs);
		if (!BreakerBody || !BreakerBody->GetStaticMesh()) continue;
		const FTransform BodyTransform=BreakerBody->GetRelativeTransform();
		const FVector Surface=GetKnobSlotCenter(Column,Row);
		const FVector Normal=BodyTransform.GetRotation().RotateVector(FVector::YAxisVector);
		auto* Knob=KnobMeshes[Column].Get();
		if(Knob->GetStaticMesh())
		{
			// Scale and rotation belong to the editable component, not the puzzle state.
			// Move its bounds center to the slot while keeping the edited cap against the face.
			const FTransform Shape(Knob->GetRelativeRotation(),FVector::ZeroVector,Knob->GetRelativeScale3D());
			const FVector Extent=Knob->CalcBounds(Shape).BoxExtent;
			const double HalfDepth=FVector::DotProduct(Extent,Normal.GetAbs());
			const FVector MeshPivotOffset=Shape.TransformPosition(Knob->GetStaticMesh()->GetBounds().Origin);
			Knob->SetRelativeLocation(Surface+Normal*HalfDepth-MeshPivotOffset);
		}
		if(KnobBounds.IsValidIndex(Column))
		{
			const FVector Top=GetKnobSlotCenter(Column,0), Bottom=GetKnobSlotCenter(Column,4);
			KnobBounds[Column]->SetRelativeLocation(FVector(9,Surface.Y,(Top.Z+Bottom.Z)*.5));
			KnobBounds[Column]->SetBoxExtent(FVector(9,7.5,(Top.Z-Bottom.Z)*.5+4));
			KnobBounds[Column]->SetCollisionEnabled(bUseIntegratedKnobs?ECollisionEnabled::QueryOnly:ECollisionEnabled::NoCollision);
		}
	}
}

bool AMD_RepairInstructionController::CanInteract_Implementation(AActor* Interactor) const
{
	return bUseIntegratedKnobs && State.bActive && State.bGuideRead && !IsValid(OperatingPawn) && GetAimedKnobColumn(Cast<APawn>(Interactor))!=INDEX_NONE;
}

void AMD_RepairInstructionController::Interact_Implementation(AActor* Interactor)
{
	APawn* Pawn=Cast<APawn>(Interactor); if(!Pawn || !Pawn->IsLocallyControlled()) return;
	if(auto* Interaction=Pawn->FindComponentByClass<UMD_InteractionComponent>()) Interaction->RequestBeginBreakerOperation(this,State.Revision);
}

void AMD_RepairInstructionController::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	BreakerBody->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue); BreakerBody->SetRenderCustomDepth(bHighlighted);
	for(UStaticMeshComponent* Mesh:KnobMeshes) { Mesh->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue); Mesh->SetRenderCustomDepth(bHighlighted); }
}

FText AMD_RepairInstructionController::GetInteractionDisplayName_Implementation() const { return NSLOCTEXT("MDInstruction","Breaker","차단기"); }

FText AMD_RepairInstructionController::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	const int32 Column=GetAimedKnobColumn(Cast<APawn>(Interactor));
	return Column!=INDEX_NONE?NSLOCTEXT("MDInstruction","OperateBreakerPrompt","[E] 차단기 조작"):FText::GetEmpty();
}

void AMD_RepairInstructionController::FinishRepair()
{
	const int32 Id=Fault->GetSnapshot().FaultId; bCompleting=true;
	const auto Sources=SourceIncidents;
	for (const auto& Pair:Sources)
		if (UMD_FacilityFaultComponent* Source=Pair.Key.Get())
			Source->CompleteRepairFromValidatedSource(Pair.Value);
	const bool bCompleted=Fault->CompleteRepairFromValidatedSource(Id); bCompleting=false;
	if (bCompleted) OnRepairCompleted.Broadcast(Id);
}

void AMD_RepairInstructionController::PublishState()
{
	if(HasAuthority() && (!State.bActive || !State.bGuideRead)) EndOperation(OperatingPawn,OperationId);
	OnRep_State(); FlushNetDormancy(); ForceNetUpdate();
}

void AMD_RepairInstructionController::OnRep_State()
{
	if (bAudioInitialized && State.bActive && State.bGuideRead && !LastAudioState.bGuideRead)
		MDAudioFeedback::World(Guide, TEXT("SFX.Facility.Instruction.Use"));
	if (bAudioInitialized && State.RestartFlashUntil > LastAudioState.RestartFlashUntil)
		MDAudioFeedback::World(Guide, TEXT("SFX.Facility.Instruction.Restart"));
	if (LastAudioState.bActive && LastAudioState.Revision == State.Revision)
		for (int32 I = 0; I < 4; ++I)
			if (State.CurrentRows.IsValidIndex(I) && LastAudioState.CurrentRows.IsValidIndex(I)
				&& State.CurrentRows[I] != LastAudioState.CurrentRows[I])
				MDAudioFeedback::At(this, TEXT("SFX.Facility.Breaker.Snap"), GetActorTransform().TransformPosition(GetKnobSlotCenter(I, State.CurrentRows[I])));
	LastAudioState = State;
	bAudioInitialized = true;
	ApplyDoorState();
	RefreshKnobs();
	if (IsValid(Guide)) Guide->RefreshPresentation();
	if (IsValid(Restart)) Restart->RefreshPresentation();
	for (AMD_RepairInstructionControl* Knob:Knobs) if (IsValid(Knob)) Knob->RefreshPresentation();
}

void AMD_RepairInstructionController::ApplyDoorState()
{
	if (!State.bActive) { ReleaseDoor(); return; }
	AActor* Door=InstructionDoor;
	if (!IsValid(Door)) return;
	FBoolProperty* Open=FindFProperty<FBoolProperty>(Door->GetClass(),TEXT("bDoorOpen"));
	FBoolProperty* Locked=FindFProperty<FBoolProperty>(Door->GetClass(),TEXT("bLocked"));
	UFunction* Apply=Door->FindFunction(TEXT("OnRep_bDoorOpen"));
	if (!Open || !Apply || Apply->NumParms!=0) return;
	if (!bDoorOwned)
	{
		ManagedDoor=Door; bDoorOwned=true; bPreviousLocked=State.bDoorPreviouslyLocked;
		for (auto* C:TInlineComponentArray<UPrimitiveComponent*>(Door))
			if (C->GetGenerateOverlapEvents()) { SuppressedSensors.Add(C); C->SetGenerateOverlapEvents(false); }
	}
	const auto* OtherFault=Door->FindComponentByClass<UMD_DoorFaultComponent>();
	const bool bClosed=State.bDoorLocked || bPreviousLocked || (OtherFault && OtherFault->IsFaultActive());
	if (HasAuthority() && Locked) Locked->SetPropertyValue_InContainer(Door,bClosed);
	if (Open->GetPropertyValue_InContainer(Door)!=!bClosed)
	{
		Open->SetPropertyValue_InContainer(Door,!bClosed); Door->ProcessEvent(Apply,nullptr);
		if (HasAuthority()) { Door->FlushNetDormancy(); Door->ForceNetUpdate(); }
	}
}

void AMD_RepairInstructionController::ReleaseDoor()
{
	if (!bDoorOwned) return;
	bDoorOwned=false; AActor* Door=ManagedDoor.Get();
	if (IsValid(Door) && !Door->IsActorBeingDestroyed())
	{
		const auto* Other=Door->FindComponentByClass<UMD_DoorFaultComponent>();
		const bool bLocked=bPreviousLocked || (Other && Other->IsFaultActive());
		if (HasAuthority())
		{
			if (auto* P=FindFProperty<FBoolProperty>(Door->GetClass(),TEXT("bLocked"))) P->SetPropertyValue_InContainer(Door,bLocked);
			if (auto* P=FindFProperty<FBoolProperty>(Door->GetClass(),TEXT("bDoorOpen")))
				if (UFunction* Apply=Door->FindFunction(TEXT("OnRep_bDoorOpen")); Apply && Apply->NumParms==0)
				{ P->SetPropertyValue_InContainer(Door,!bLocked); Door->ProcessEvent(Apply,nullptr); }
			Door->FlushNetDormancy(); Door->ForceNetUpdate();
		}
	}
	auto Sensors=MoveTemp(SuppressedSensors);
	for (const auto& C:Sensors) if (C.IsValid()) C->SetGenerateOverlapEvents(true);
	ManagedDoor.Reset();
}

void AMD_RepairInstructionController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!State.bActive) return;
	if (HasAuthority())
	{
		if(IsValid(OperatingPawn) && !CanContinueOperation(OperatingPawn,State.Revision))
		{
			FVector Eye; FRotator Rotation; OperatingPawn->GetActorEyesViewPoint(Eye,Rotation);
			UE_LOG(LogTemp,Verbose,TEXT("[BreakerUI] Operator reach invalid eye=%s target=%s"),*Eye.ToString(),*BreakerInteractionBounds->GetComponentLocation().ToString());
			EndOperation(OperatingPawn,OperationId);
		}
		if (!HasValidConfiguration()) { Fault->CompleteRepairFromValidatedSource(Fault->GetSnapshot().FaultId); return; }
		bool bAliveReader=false;
		for(FConstPlayerControllerIterator It=GetWorld()->GetPlayerControllerIterator();It;++It)
		{
			const APawn* Pawn=It->IsValid()?It->Get()->GetPawn():nullptr;
			const auto* Interaction=IsValid(Pawn)?Pawn->FindComponentByClass<UMD_InteractionComponent>():nullptr;
			if(Interaction && Interaction->CanOwnerInteract() && IsInInstructionRoom(Pawn)) { bAliveReader=true; break; }
		}
		if (State.bDoorLocked && (GetParticipantCount()==1 || !bAliveReader)) { State.bDoorLocked=false; PublishState(); }
	}
	ApplyDoorState();
}

AMD_RepairInstructionControl::AMD_RepairInstructionControl()
{
	bReplicates=true; bAlwaysRelevant=true; PrimaryActorTick.bCanEverTick=true; PrimaryActorTick.TickInterval=0.1f;
	InteractionBounds=CreateDefaultSubobject<UBoxComponent>(TEXT("InteractionBounds")); SetRootComponent(InteractionBounds);
	InteractionBounds->SetBoxExtent(FVector(6,65,70)); InteractionBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	InteractionBounds->SetCollisionResponseToAllChannels(ECR_Ignore); InteractionBounds->SetCollisionResponseToChannel(ECC_Visibility,ECR_Block);
	InteractionBounds->SetCollisionResponseToChannel(MDCollisionChannels::InteractTrace,ECR_Block);
	Body=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body")); Body->SetupAttachment(RootComponent); Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube")); if(Cube.Succeeded()) Body->SetStaticMesh(Cube.Object);
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GuideMaterial(TEXT("/Game/Assets/X7/Interior/Objects/RepairInstruction/M_RepairDisplay_Instruction.M_RepairDisplay_Instruction"));
	if(GuideMaterial.Succeeded()) GuideLightMaterial=GuideMaterial.Object;
	AuxiliaryLightBody=CreateDefaultSubobject<UStaticMeshComponent>(TEXT("AuxiliaryLightBody")); AuxiliaryLightBody->SetupAttachment(RootComponent); AuxiliaryLightBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Heading=CreateDefaultSubobject<UTextRenderComponent>(TEXT("Heading")); Heading->SetupAttachment(RootComponent);
	Heading->SetHorizontalAlignment(EHTA_Center); Heading->SetWorldSize(8); Heading->SetRelativeLocation(FVector(7,0,72));
	AuxiliaryLight=CreateDefaultSubobject<USpotLightComponent>(TEXT("AuxiliaryLight")); AuxiliaryLight->SetupAttachment(RootComponent);
	AuxiliaryLight->SetRelativeLocation(FVector(45,0,110)); AuxiliaryLight->SetRelativeRotation(FRotator(-70,180,0));
	AuxiliaryLight->SetIntensity(600); AuxiliaryLight->SetAttenuationRadius(240); AuxiliaryLight->SetOuterConeAngle(38); AuxiliaryLight->SetCastShadows(false);
}

void AMD_RepairInstructionControl::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AMD_RepairInstructionControl,Controller);
	DOREPLIFETIME(AMD_RepairInstructionControl,ControlKind); DOREPLIFETIME(AMD_RepairInstructionControl,Column);
}

void AMD_RepairInstructionControl::RemoveLegacyOverlayComponents()
{
	// Placed actors can retain serialized native subobjects after their constructor definitions are removed.
	if(HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject)) return;
	TInlineComponentArray<UActorComponent*> Components(this);
	for(UActorComponent* Component:Components)
	{
		const FString Name=Component->GetName();
		auto IsIndexedName=[&Name](const TCHAR* Prefix,int32 Count)
		{
			const int32 PrefixLength=FCString::Strlen(Prefix);
			return Name.StartsWith(Prefix) && Name.Len()==PrefixLength+2
				&& FChar::IsDigit(Name[PrefixLength]) && FChar::IsDigit(Name[PrefixLength+1])
				&& FCString::Atoi(*Name.Right(2))<Count;
		};
		const bool bOldMesh=Component->IsA<UStaticMeshComponent>() && (Name==TEXT("ScreenSurface") || IsIndexedName(TEXT("Marker_"),28));
		const bool bOldText=Component->IsA<UTextRenderComponent>() && IsIndexedName(TEXT("Label_"),9);
		if(bOldMesh || bOldText) Component->DestroyComponent();
	}
}

void AMD_RepairInstructionControl::PostLoad() { Super::PostLoad(); RemoveLegacyOverlayComponents(); }
void AMD_RepairInstructionControl::OnConstruction(const FTransform& Transform) { Super::OnConstruction(Transform); RefreshPresentation(); }

namespace
{
void FitInstructionMesh(UStaticMeshComponent* Component, const FVector& Center, const FVector& Size)
{
	const UStaticMesh* Mesh=Component->GetStaticMesh(); if(!Mesh) return;
	const auto Bounds=Mesh->GetBounds(); const FVector NativeSize=Bounds.BoxExtent*2.;
	if(NativeSize.GetMin()<=SMALL_NUMBER) return;
	const FQuat Rotation=Component->GetRelativeRotation().Quaternion();
	const FVector Scale=Rotation.UnrotateVector(Size).GetAbs()/NativeSize;
	Component->SetRelativeScale3D(Scale); Component->SetRelativeLocation(Center-Rotation.RotateVector(Bounds.Origin*Scale));
}
}

FVector2D AMD_RepairInstructionControl::GetGuideCircleUV(int32 Column,int32 Row)
{
	if(Column<0 || Column>=4 || Row<0 || Row>=5) return FVector2D(-1,-1);
	// Original 2048px atlas ring centers, matched to physical left-to-right columns and top-to-bottom rows.
	static constexpr int32 Pixels[4][5][2]={
		{{1434,743},{1354,739},{1276,735},{1199,731},{1122,727}},
		{{1428,876},{1348,872},{1269,869},{1192,865},{1115,861}},
		{{1421,1008},{1341,1003},{1262,999},{1186,995},{1109,991}},
		{{1415,1142},{1335,1137},{1256,1133},{1179,1130},{1102,1126}}};
	return FVector2D(Pixels[Column][Row][0]/2048.,Pixels[Column][Row][1]/2048.);
}

void AMD_RepairInstructionControl::RefreshPresentation()
{
	RemoveLegacyOverlayComponents();
	const bool bGuide=ControlKind==EMD_InstructionControlKind::Guide;
	const bool bKnob=ControlKind==EMD_InstructionControlKind::Knob;
	const bool bMeshRestart=ControlKind==EMD_InstructionControlKind::Restart && IsValid(Controller) && IsValid(Controller->Guide);
	const bool bActive=IsValid(Controller) && Controller->State.bActive;
	const auto* State=IsValid(Controller)?&Controller->State:nullptr;
	FitInstructionMesh(Body,FVector::ZeroVector,bGuide?FVector(4,125,130):bKnob?FVector(5,24,120):FVector(5,50,25));
	Body->SetVisibility(!bMeshRestart);
	AuxiliaryLightBody->SetVisibility(bGuide);
	InteractionBounds->SetBoxExtent(bGuide?FVector(7,65,70):bKnob?FVector(7,14,66):FVector(7,28,17));
	if(bMeshRestart)
	{
		const auto* Guide=Controller->Guide.Get();
		// Put this invisible interaction proxy in front of the guide's broad query box.
		SetActorTransform(FTransform(Guide->GetActorRotation(),Guide->GetRestartButtonWorldLocation()
			+Guide->GetActorForwardVector()*7.*FMath::Abs(Guide->GetActorScale3D().X),Guide->GetActorScale3D()));
		InteractionBounds->SetBoxExtent(FVector(2,7,7));
	}
	AuxiliaryLight->SetVisibility(bGuide && bActive);
	Heading->SetRelativeLocation(FVector(8,0,bGuide?72:bKnob?70:0));
	Heading->SetText(FText::FromString(bGuide?TEXT("REPAIR INSTRUCTIONS"):bKnob?FString::Printf(TEXT("COL %d"),Column+1):TEXT("RESTART\nOPEN DOOR")));
	Heading->SetVisibility(!bGuide && !bKnob && !bMeshRestart);
	if(bGuide && GuideLightMaterial)
	{
		if(!GuideMID || Body->GetMaterial(0)!=GuideMID)
			GuideMID=Body->CreateDynamicMaterialInstance(0,GuideLightMaterial);
		if(GuideMID)
		{
			const bool bValidAnswer=State && State->TargetRows.Num()==4 && !State->TargetRows.ContainsByPredicate([](int32 R) { return R<0 || R>=5; });
			GuideMID->SetScalarParameterValue(TEXT("GuideActive"),bActive && State->bGuideRead && bValidAnswer?1.f:0.f);
			GuideMID->SetScalarParameterValue(TEXT("GuideGlowStrength"),FMath::Clamp(GuideGlowStrength,0.f,20.f));
			for(int32 C=0;C<4;++C)
			{
				const FVector2D UV=GetGuideCircleUV(C,bValidAnswer?State->TargetRows[C]:0);
				GuideMID->SetVectorParameterValue(*FString::Printf(TEXT("GuideTarget%d"),C),FLinearColor(UV.X,UV.Y,0,0));
			}
			UpdateRestartFlash();
		}
	}
	if(bGuide && IsValid(Controller) && IsValid(Controller->Restart) && Controller->Restart!=this)
		Controller->Restart->RefreshPresentation();
	PresentedRevision=State?State->Revision:0; PresentedRow=State && State->CurrentRows.IsValidIndex(Column)?State->CurrentRows[Column]:0; bPresentedActive=bActive;
}

void AMD_RepairInstructionControl::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if(ControlKind==EMD_InstructionControlKind::Guide) UpdateRestartFlash();
	if(IsValid(Controller) && (PresentedRevision!=Controller->State.Revision || bPresentedActive!=Controller->State.bActive
		|| (ControlKind==EMD_InstructionControlKind::Knob && Controller->State.CurrentRows.IsValidIndex(Column) && PresentedRow!=Controller->State.CurrentRows[Column]))) RefreshPresentation();
}

FVector AMD_RepairInstructionControl::GetRestartButtonWorldLocation() const
{
	return Body->GetComponentTransform().TransformPosition(FVector(24.236723,12.653073,20.287705));
}

void AMD_RepairInstructionControl::UpdateRestartFlash()
{
	if(!GuideMID) return;
	const auto* GS=GetWorld()?GetWorld()->GetGameState():nullptr;
	const double Remaining=IsValid(Controller) && Controller->State.bActive && GS
		?Controller->State.RestartFlashUntil-GS->GetServerWorldTimeSeconds():0.;
	GuideMID->SetScalarParameterValue(TEXT("RestartPulse"),FMath::Clamp(Remaining/FMath::Clamp(RestartFlashSeconds,.1f,3.f),0.,1.));
}

bool AMD_RepairInstructionControl::CanInteract_Implementation(AActor* Interactor) const
{
	return IsValid(Controller) && Controller->State.bActive && IsValid(Interactor) && Interactor->IsA<APawn>()
		&& (ControlKind!=EMD_InstructionControlKind::Knob || !Controller->bUseIntegratedKnobs)
		&& (ControlKind!=EMD_InstructionControlKind::Knob || Controller->State.bGuideRead);
}

void AMD_RepairInstructionControl::Interact_Implementation(AActor* Interactor)
{
	if(ControlKind==EMD_InstructionControlKind::Restart) return;
	const APawn* Pawn=Cast<APawn>(Interactor);
	// Never route a remote server's generic Interact back through a fresh revision.
	if(!IsValid(Controller) || !Pawn || !Pawn->IsLocallyControlled()) return;
	if(auto* Interaction=IsValid(Interactor)?Interactor->FindComponentByClass<UMD_InteractionComponent>():nullptr)
		Interaction->RequestInstructionRepair(this,Controller->State.Revision);
}

void AMD_RepairInstructionControl::SetInteractionHighlighted_Implementation(bool bHighlighted)
{
	UStaticMeshComponent* Visual=ControlKind==EMD_InstructionControlKind::Restart && IsValid(Controller) && IsValid(Controller->Guide)
		?Controller->Guide->Body.Get():Body.Get();
	Visual->SetCustomDepthStencilValue(IMD_Interactable::HighlightStencilValue); Visual->SetRenderCustomDepth(bHighlighted);
}

FText AMD_RepairInstructionControl::GetInteractionDisplayName_Implementation() const
{
	if(ControlKind==EMD_InstructionControlKind::Guide) return NSLOCTEXT("MDInstruction","Guide","수리 지침");
	if(ControlKind==EMD_InstructionControlKind::Restart) return NSLOCTEXT("MDInstruction","Restart","지침 재시작");
	return FText::Format(NSLOCTEXT("MDInstruction","Knob","차단기 {0}열"),FText::AsNumber(Column+1));
}

FText AMD_RepairInstructionControl::GetInteractionPrompt_Implementation(AActor* Interactor) const
{
	// 수리 사건이 없는 장치의 정상 상태 안내 유지
	if (IsValid(Controller) == false || Controller->State.bActive == false)
	{
		// 비활성 상태에서는 조작 행동 대신 전력 상태 표시
		return NSLOCTEXT("MDInstruction", "Idle", "전력 정상");
	}
	// 재시작 장치에 지정된 행동 유형 선택
	if (ControlKind == EMD_InstructionControlKind::Restart)
	{
		// 유형별 행동 문구를 소유한 프로젝트 설정 조회
		const UMD_InteractionSettings* PromptSettings = UMD_InteractionSettings::Get();
		// 설정 준비 실패 시 잘못된 안내 표시 방지
		if (IsValid(PromptSettings) == false)
		{
			// 조회할 수 없는 행동 문구의 안내 생략
			return FText::GetEmpty();
		}
		// 빈 문구 검사와 입력 안내 결합에서 사용할 재시작 행동 보관
		FText ActionPrompt = PromptSettings->GetPromptText(RestartPromptType);
		// 표시 안 함 유형과 빈 문구 설정 존중
		if (ActionPrompt.IsEmpty() == true)
		{
			// 행동 문구가 없는 경우 입력 키만 남는 상황 방지
			return FText::GetEmpty();
		}
		// 기본 재시작 입력인 도구 사용 키 준비
		FKey AbilityKey = EKeys::LeftMouseButton;
		// 현재 플레이어에게 적용된 입력 설정 조회
		UMD_SettingsManager* Settings = UMD_SettingsManager::Get(Interactor);
		// 설정 관리자 준비 여부 확인
		if (IsValid(Settings) == true)
		{
			// 편집 중인 값과 구분되는 적용 입력 카테고리 조회
			UMD_ControlSettingCategory* Controls = Settings->GetControlSettings();
			// 적용 입력 카테고리 준비 여부 확인
			if (IsValid(Controls) == true)
			{
				// 실제 재시작 조작에 연결된 도구 사용 키 보관
				FKey AppliedKey = Controls->GetAppliedSettings().AbilityKey;
				// 유효한 재지정 키만 기본 입력에 우선 적용
				if (AppliedKey.IsValid() == true)
				{
					// 화면 안내와 실제 재시작 입력의 일치
					AbilityKey = AppliedKey;
				}
			}
		}
		// 기본 좌클릭 표현과 재지정 키 이름을 반영한 행동 안내 반환
		return FText::Format(NSLOCTEXT("MDInstruction", "TypedRestartClickPrompt", "[{0}] {1}"),
			AbilityKey == EKeys::LeftMouseButton ? NSLOCTEXT("MDInstruction", "LeftMouseButton", "좌클릭") : AbilityKey.GetDisplayName(false), ActionPrompt);
	}
	// 지침 장치에 지정된 행동 유형 선택
	if (ControlKind == EMD_InstructionControlKind::Guide)
	{
		// 현재 입력 키와 지침 행동의 설정 문구 반환
		return UMD_InteractionPresentationLibrary::FindActionPromptByType(GuidePromptType, Interactor);
	}
	// 노브 행동의 설정 문구를 빈 안내 검사와 현재 행 표시에서 공유
	FText ActionPrompt = UMD_InteractionPresentationLibrary::FindActionPromptByType(KnobPromptType, Interactor);
	// 표시 안 함 유형이나 비어 있는 설정 문구 존중
	if (ActionPrompt.IsEmpty() == true)
	{
		// 행동 문구가 없는 경우 부가 행 정보도 생략
		return FText::GetEmpty();
	}
	// 기존 노브 행 위치를 선택한 행동 문구 뒤에 표시
	return FText::Format(NSLOCTEXT("MDInstruction", "TypedKnobPrompt", "{0} · 현재 위에서 {1}칸"), ActionPrompt,
		FText::AsNumber(Controller->State.CurrentRows.IsValidIndex(Column) ? Controller->State.CurrentRows[Column] + 1 : 1));
}

#if !UE_BUILD_SHIPPING
static FAutoConsoleCommandWithWorld GMDTestBlackout(
	TEXT("MD.Blackout"), TEXT("Trigger the configured breaker's linked wire faults and repair instructions. Host/server only."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World)
	{
		if (!World || !World->IsGameWorld() || World->GetNetMode()==NM_Client)
		{
			UE_LOG(LogTemp, Display, TEXT("[MD.Blackout] Run this command during play on the host/server."));
			return;
		}
		for (TActorIterator<AMD_RepairInstructionController> It(World); It; ++It)
		{
			if (!It->HasValidConfiguration()) continue;
			if (It->State.bActive)
			{
				UE_LOG(LogTemp, Display, TEXT("[MD.Blackout] %s is already active; the current incident is preserved."), *It->GetName());
				return;
			}
			int32 BrokenWires=0;
			for (AMD_PowerConduit* Wire : It->PowerConduits)
			{
				if (!IsValid(Wire) || Wire->GetWorld()!=World || !IsValid(Wire->FacilityRepair)) continue;
				// Unconfigured maps can preview blackout without saving new map references.
				// Restrict the fallback to the wire's exact normal material and slot.
				if (Wire->TargetActors.IsEmpty() && Wire->MeshComponents.IsEmpty()
					&& Wire->NormalMaterial && Wire->BlackoutMaterial && Wire->MaterialIndex>=0)
				{
					for (TActorIterator<AActor> Target(World); Target; ++Target)
					{
						TInlineComponentArray<UStaticMeshComponent*> Meshes;
						Target->GetComponents(Meshes);
						for (UStaticMeshComponent* Mesh : Meshes)
							if (IsValid(Mesh) && Wire->MaterialIndex<Mesh->GetNumMaterials()
								&& Mesh->GetMaterial(Wire->MaterialIndex)==Wire->NormalMaterial)
							{
								FComponentReference Reference; Reference.OverrideComponent=Mesh;
								Wire->MeshComponents.Add(Reference);
							}
						}
					UE_LOG(LogTemp, Display, TEXT("[MD.Blackout] %s: temporarily connected %d matching power meshes for this play session."),
						*Wire->GetName(), Wire->MeshComponents.Num());
				}
				Wire->ApplyPoweredLights();
				// Use the normal wire-fault notification path for power, effects and the linked puzzle.
				if (Wire->FacilityRepair->IsFaultActive() || Wire->FacilityRepair->TryBreak()) ++BrokenWires;
			}
			if (BrokenWires>0)
			{
				UE_LOG(LogTemp, Display, TEXT("[MD.Blackout] %s: %d linked wire fault(s), repair instructions %s."),
					*It->GetName(), BrokenWires, It->State.bActive?TEXT("active"):TEXT("inactive"));
				return;
			}
		}
		UE_LOG(LogTemp, Display, TEXT("[MD.Blackout] No configured breaker with an available linked wire was found."));
	}), ECVF_Cheat);

static FAutoConsoleCommandWithWorld GMDStartInstruction(TEXT("MD.BreakInstruction"),TEXT("Start the configured repair-instruction incident on the host/server; does not schedule or create a wire blackout."),
	FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* World) { if(World && World->IsGameWorld() && World->GetNetMode()!=NM_Client) for(TActorIterator<AMD_RepairInstructionController> It(World);It;++It) if(It->StartRepairIncident()) break; }),ECVF_Cheat);
#endif
