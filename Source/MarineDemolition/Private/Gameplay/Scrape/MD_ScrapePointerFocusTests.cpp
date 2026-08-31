// Copyright Epic Games, Inc. All Rights Reserved.

#include "Component/Scrape/MD_ScrapeToolComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Camera/CameraTypes.h"
#include "Character/MD_Character.h"
#include "Component/Equipment/MD_EquipHotbarComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Data/Equipment/MD_ToolDefinition.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Scrape/MD_ScrapeAimValidation.h"
#include "Gameplay/Scrape/MD_ScrapeFocusProjection.h"
#include "GameplayTags/MD_GameplayTags.h"
#include "Misc/AutomationTest.h"
#include "World/Wall/MD_CuttableWall.h"

#include <limits>

namespace
{
	class FMDPointerFocusTestWorld final
	{
	public:
		FMDPointerFocusTestWorld()
		{
			if (!GEngine)
			{
				return;
			}

			const FName WorldName = MakeUniqueObjectName(
				nullptr,
				UWorld::StaticClass(),
				TEXT("MDPointerFocusTestWorld"),
				EUniqueObjectNameOptions::GloballyUnique);
			World = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				WorldName,
				GetTransientPackage());
			if (!World)
			{
				return;
			}

			World->AddToRoot();
			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(
				EWorldType::Game);
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FMDPointerFocusTestWorld()
		{
			if (!World)
			{
				return;
			}

			if (GEngine)
			{
				GEngine->ShutdownWorldNetDriver(World);
			}
			World->RemoveFromRoot();
			if (GEngine)
			{
				GEngine->DestroyWorldContext(World);
			}
			World->DestroyWorld(false);
			World = nullptr;
		}

		UWorld* Get() const
		{
			return World;
		}

	private:
		UWorld* World = nullptr;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMD_ScrapePointerNormalizationTest,
	"MarineDemolition.Scrape.Focus.PointerNormalization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMD_ScrapePointerNormalizationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const FIntPoint ConstrainedViewportSize(1600, 900);
	FVector2D NormalizedPointer(-1.0, -1.0);
	TestTrue(
		TEXT("A cursor in the constrained player view is normalized against that view."),
		FMD_ScrapeFocusProjection::TryNormalizeViewportPointer(
			FVector2D(400.0, 675.0),
			ConstrainedViewportSize,
			NormalizedPointer));
	TestTrue(
		TEXT("The constrained-view cursor preserves its exact normalized position."),
		NormalizedPointer.Equals(FVector2D(0.25, 0.75), 1.0e-6));

	NormalizedPointer = FVector2D(-1.0, -1.0);
	TestTrue(
		TEXT("The lower-right view boundary is accepted."),
		FMD_ScrapeFocusProjection::TryNormalizeViewportPointer(
			FVector2D(1600.0, 900.0),
			ConstrainedViewportSize,
			NormalizedPointer));
	TestTrue(
		TEXT("The lower-right boundary maps to one."),
		NormalizedPointer.Equals(FVector2D(1.0, 1.0), 1.0e-6));

	NormalizedPointer = FVector2D(-1.0, -1.0);
	TestFalse(
		TEXT("A stale cursor outside the current player view is rejected."),
		FMD_ScrapeFocusProjection::TryNormalizeViewportPointer(
			FVector2D(1600.01, 450.0),
			ConstrainedViewportSize,
			NormalizedPointer));
	TestTrue(
		TEXT("A rejected cursor yields the safe center fallback."),
		NormalizedPointer.Equals(FVector2D(0.5, 0.5), 1.0e-6));

	NormalizedPointer = FVector2D(-1.0, -1.0);
	TestFalse(
		TEXT("A zero-sized current view cannot bootstrap a cursor."),
		FMD_ScrapeFocusProjection::TryNormalizeViewportPointer(
			FVector2D::ZeroVector,
			FIntPoint::ZeroValue,
			NormalizedPointer));
	TestTrue(
		TEXT("An unavailable view also yields the center fallback."),
		NormalizedPointer.Equals(FVector2D(0.5, 0.5), 1.0e-6));

	NormalizedPointer = FVector2D(-1.0, -1.0);
	TestFalse(
		TEXT("A non-finite cursor cannot escape into pointer state."),
		FMD_ScrapeFocusProjection::TryNormalizeViewportPointer(
			FVector2D(
				std::numeric_limits<double>::quiet_NaN(),
				450.0),
			ConstrainedViewportSize,
			NormalizedPointer));
	TestTrue(
		TEXT("A non-finite cursor is replaced by the center fallback."),
		NormalizedPointer.Equals(FVector2D(0.5, 0.5), 1.0e-6));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMD_ScrapeFocusCameraLockTest,
	"MarineDemolition.Scrape.Focus.LocalViewLatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMD_ScrapeFocusCameraLockTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UMD_ScrapeToolComponent* const Tool =
		NewObject<UMD_ScrapeToolComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("A scrape component can model the local view latch."), Tool))
	{
		return false;
	}

	const FVector2D FreePointer = Tool->GetScrapeFocusPointerNormalized();
	TestFalse(
		TEXT("Free mode does not consume look input."),
		Tool->ConsumeScrapeFocusLookInput(FVector2D(1.0, 1.0)));
	TestTrue(
		TEXT("Free mode leaves the virtual pointer untouched."),
		Tool->GetScrapeFocusPointerNormalized().Equals(FreePointer));

	const FRotator LockedRotation(-12.0, 37.0, 3.0);
	constexpr float LockedFOV = 81.0f;
	Tool->bScrapeFocusViewActive = true;
	Tool->bHasScrapeFocusView = true;
	Tool->ScrapeFocusState = EMD_ScrapeFocusState::Free;
	Tool->ScrapeFocusViewRotation = LockedRotation;
	Tool->ScrapeFocusViewFOV = LockedFOV;
	Tool->ScrapeFocusViewportSize = FIntPoint(1920, 1080);
	Tool->ScrapeFocusSession.ScrapeFocusAspectRatio = 16.0f / 9.0f;
	Tool->ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent = 1.0f;
	Tool->ScrapeFocusSession.ScrapeFocusVerticalHalfTangent = 9.0f / 16.0f;
	Tool->ScrapeFocusSession.ScrapeFocusAuthorityHorizontalHalfTangent = 1.0f;
	Tool->ScrapeFocusSession.ScrapeFocusAuthorityVerticalHalfTangent = 9.0f / 16.0f;
	Tool->ScrapeFocusPointerNormalized = FVector2D(0.5, 0.5);
	Tool->ScrapeLoad = 46.25f;
	Tool->ContaminationPaint.SaturatedContaminationStampPatternSeed = 9917;

	FMinimalViewInfo View;
	View.Location = FVector(123.0, -45.0, 67.0);
	View.Rotation = FRotator(20.0, -90.0, 15.0);
	View.FOV = 55.0f;
	View.DesiredFOV = 60.0f;
	const FVector OriginalLocation = View.Location;
	TestTrue(
		TEXT("A targetless local latch still applies its fixed display view."),
		Tool->ApplyScrapeFocusCameraView(View));
	TestTrue(
		TEXT("The display rotation and FOV remain frozen while authority is Free."),
		View.Rotation.Equals(LockedRotation, 0.001f)
			&& FMath::IsNearlyEqual(View.FOV, LockedFOV, 0.001f)
			&& FMath::IsNearlyEqual(View.DesiredFOV, LockedFOV, 0.001f));
	TestTrue(
		TEXT("The camera origin remains owned by normal camera evaluation."),
		View.Location.Equals(OriginalLocation, 0.001f));
	TestTrue(
		TEXT("The local view latch is independent from the network Focus state."),
		Tool->IsScrapeFocusViewActive()
			&& Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::Free
			&& Tool->GetActiveScrapeFocusSessionId() == 0);

	const FVector CenterAim = Tool->GetScrapeFocusVisualAimDirection();
	Tool->ScrapeFocusPointerNormalized += FVector2D(0.15, -0.10);
	Tool->ClampScrapeFocusPointerToAuthorityCone();
	const FVector OffsetAim = Tool->GetScrapeFocusVisualAimDirection();
	const FRotationMatrix LockedAxes(LockedRotation);
	TestTrue(
		TEXT("Moving the virtual pointer changes the display ray."),
		!OffsetAim.Equals(CenterAim, 1.0e-5)
			&& FVector::DotProduct(
				OffsetAim,
				LockedAxes.GetScaledAxis(EAxis::Y)) > 0.0
			&& FVector::DotProduct(
				OffsetAim,
				LockedAxes.GetScaledAxis(EAxis::Z)) > 0.0);
	TestTrue(
		TEXT("Pointer movement never changes the frozen camera basis."),
		Tool->ScrapeFocusViewRotation.Equals(LockedRotation, 0.001f));

	const FVector2D PointerBeforeInvalidLook =
		Tool->GetScrapeFocusPointerNormalized();
	TestTrue(
		TEXT("The local latch owns look routing even while authority remains Free."),
		Tool->ConsumeScrapeFocusLookInput(FVector2D(
			std::numeric_limits<double>::quiet_NaN(),
			0.0)));
	TestTrue(
		TEXT("Invalid look input cannot contaminate the virtual pointer."),
		Tool->GetScrapeFocusPointerNormalized().Equals(
			PointerBeforeInvalidLook,
			1.0e-6));
	TestTrue(
		TEXT("View-only pointer work cannot alter scrape load or contamination policy."),
		FMath::IsNearlyEqual(Tool->GetScrapeLoad(), 46.25f)
			&& Tool->ContaminationPaint.SaturatedContaminationStampPatternSeed == 9917);

	Tool->ScrapeVisualContactPose.bValid = true;
	Tool->ScrapeVisualContactPose.UpdateGeneration = 7;
	Tool->EndScrapeFocusInternal(false);
	FMD_ScrapeVisualContactPose EndedPose;
	TestTrue(
		TEXT("A second RMB-style end clears both local and authority state."),
		!Tool->IsScrapeFocusViewActive()
			&& Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::Free
			&& Tool->GetActiveScrapeFocusSessionId() == 0
			&& !Tool->GetScrapeVisualContactPose(EndedPose));
	TestTrue(
		TEXT("Ending view mode restores the center fallback pointer."),
		Tool->GetScrapeFocusPointerNormalized().Equals(
			FVector2D(0.5, 0.5),
			1.0e-6));
	Tool->EndScrapeFocusInternal(false);
	TestTrue(
		TEXT("Repeated cleanup remains idempotent."),
		!Tool->IsScrapeFocusViewActive()
			&& Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::Free);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMD_ScrapeNetworkSessionTest,
	"MarineDemolition.Scrape.Network.PointerFocusLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMD_ScrapeNetworkSessionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	if (!TestNotNull(TEXT("GEngine is available for pointer Focus lifecycle tests."), GEngine))
	{
		return false;
	}

	UMD_ScrapeToolComponent* const RemoteTool =
		NewObject<UMD_ScrapeToolComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("A remote-proxy component fixture can be created."), RemoteTool))
	{
		return false;
	}
	RemoteTool->bIsScraping = true;
	RemoteTool->ScrapeLoad = 63.0f;
	RemoteTool->ContaminationPaint.SaturatedContaminationStampPatternSeed = 4411;
	RemoteTool->ContaminationPaint.CommittedStage3Dabs.AddDefaulted();
	const int32 RemoteCommittedDabCount =
		RemoteTool->ContaminationPaint.CommittedStage3Dabs.Num();
	TestFalse(
		TEXT("A component without a locally controlled owner cannot create a view latch."),
		RemoteTool->TryBeginScrapeFocus());
	FMD_ScrapeVisualContactPose RemotePose;
	TestTrue(
		TEXT("A remote proxy has no local latch or animation-only visual pose."),
		!RemoteTool->IsScrapeFocusViewActive()
			&& !RemoteTool->GetScrapeVisualContactPose(RemotePose));
	TestTrue(
		TEXT("A rejected remote latch cannot alter legacy scrape or contamination state."),
		RemoteTool->IsScraping()
			&& FMath::IsNearlyEqual(RemoteTool->GetScrapeLoad(), 63.0f)
			&& RemoteTool->ContaminationPaint.SaturatedContaminationStampPatternSeed == 4411
			&& RemoteTool->ContaminationPaint.CommittedStage3Dabs.Num()
				== RemoteCommittedDabCount);

	FMDPointerFocusTestWorld TestWorld;
	UWorld* const World = TestWorld.Get();
	if (!TestNotNull(TEXT("A transient game world can be created."), World))
	{
		return false;
	}

	AMD_Character* const Character = World->SpawnActor<AMD_Character>();
	APlayerController* const Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("A local character can be spawned."), Character)
		|| !TestNotNull(TEXT("A local controller can be spawned."), Controller))
	{
		return false;
	}
	ULocalPlayer* const LocalPlayer = NewObject<ULocalPlayer>(
		GEngine);
	if (!TestNotNull(TEXT("A local-player identity can be created."), LocalPlayer))
	{
		return false;
	}
	Controller->SetPlayer(LocalPlayer);
	Controller->Possess(Character);
	Controller->SetControlRotation(FRotator::ZeroRotator);
	Character->SetActorRotation(FRotator::ZeroRotator);
	if (!Character->HasActorBegunPlay())
	{
		Character->DispatchBeginPlay();
	}
	if (!Controller->HasActorBegunPlay())
	{
		Controller->DispatchBeginPlay();
	}
	TestTrue(
		TEXT("The authority fixture is also its locally controlled owner."),
		Character->IsLocallyControlled());

	UMD_EquipHotbarComponent* const Hotbar = Character->GetHotbarComponent();
	UMD_ScrapeToolComponent* const Tool = Character->GetScrapeToolComponent();
	if (!TestNotNull(TEXT("The character owns a hotbar."), Hotbar)
		|| !TestNotNull(TEXT("The character owns a scrape component."), Tool))
	{
		return false;
	}
	UMD_ToolDefinition* const ScraperDefinition =
		NewObject<UMD_ToolDefinition>(Character, TEXT("PointerFocusScraperDefinition"));
	ScraperDefinition->ToolAbilityTag = MDGameplayTags::Ability_Scrape;
	Hotbar->SetToolAtSlot(0, ScraperDefinition);
	if (!TestTrue(
		TEXT("The lifecycle fixture equips a scraper definition."),
		Hotbar->GetCurrentTool() == ScraperDefinition))
	{
		return false;
	}

	auto SeedLocalView = [&] (const uint32 ViewGeneration)
	{
		Tool->EndScrapeFocusInternal(false);
		FVector ViewOrigin = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		Controller->GetPlayerViewPoint(ViewOrigin, ViewRotation);
		Tool->ScrapeFocusViewOrigin = ViewOrigin;
		Tool->ScrapeFocusViewRotation = ViewRotation;
		Tool->ScrapeFocusViewFOV = Controller->PlayerCameraManager
			? Controller->PlayerCameraManager->GetFOVAngle()
			: 90.0f;
		Tool->ScrapeFocusViewportSize = FIntPoint(1920, 1080);
		Tool->ScrapeFocusSession.ScrapeFocusAspectRatio = 16.0f / 9.0f;
		Tool->ScrapeFocusSession.ScrapeFocusHorizontalHalfTangent = 1.0f;
		Tool->ScrapeFocusSession.ScrapeFocusVerticalHalfTangent = 9.0f / 16.0f;
		Tool->ScrapeFocusSession.ScrapeFocusAuthorityHorizontalHalfTangent = 1.0f;
		Tool->ScrapeFocusSession.ScrapeFocusAuthorityVerticalHalfTangent = 9.0f / 16.0f;
		Tool->ScrapeFocusPointerNormalized = FVector2D(0.5, 0.5);
		Tool->bHasScrapeFocusView = true;
		Tool->bScrapeFocusViewActive = true;
		Tool->bScrapeFocusViewBeginAttemptConsumed = false;
		Tool->bLastScrapeFocusViewAimActionable = false;
		Tool->ScrapeFocusViewGeneration = ViewGeneration;
		Tool->PendingScrapeFocusViewGeneration = 0;
		Tool->ScrapeFocusViewTool = ScraperDefinition;
		Tool->ScrapeFocusState = EMD_ScrapeFocusState::Free;
		Tool->CurrentScrapeAim = FMD_ScrapeAimQueryResult();
	};

	SeedLocalView(11);
	Tool->ScrapeLoad = 27.5f;
	Tool->ContaminationPaint.SaturatedContaminationStampPatternSeed = 7733;
	const uint32 TargetlessFocusGeneration =
		Tool->ScrapeFocusSession.ScrapeFocusGeneration;
	TestFalse(
		TEXT("A targetless local latch does not request an authority session."),
		Tool->TryBeginAuthoritativeScrapeFocusFromCurrentAim());
	TestTrue(
		TEXT("Targetless view mode stays latched while network Focus remains Free."),
		Tool->IsScrapeFocusViewActive()
			&& Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::Free
			&& Tool->GetActiveScrapeFocusSessionId() == 0
			&& !Tool->bScrapeFocusViewBeginAttemptConsumed
			&& Tool->ScrapeFocusSession.ScrapeFocusGeneration
				== TargetlessFocusGeneration);
	TestTrue(
		TEXT("Targetless polling cannot mutate load or contamination state."),
		FMath::IsNearlyEqual(Tool->GetScrapeLoad(), 27.5f)
			&& Tool->ContaminationPaint.SaturatedContaminationStampPatternSeed == 7733);

	Tool->CurrentScrapeAim.Type = EMD_ScrapeAimQueryType::SurfaceHit;
	const uint32 FocusGenerationBeforeActionableEdge =
		Tool->ScrapeFocusSession.ScrapeFocusGeneration;
	TestTrue(
		TEXT("The first actionable edge consumes exactly one Begin attempt."),
		Tool->TryBeginAuthoritativeScrapeFocusFromCurrentAim());
	TestTrue(
		TEXT("The actionable edge advances one Focus generation and consumes the latch."),
		Tool->bScrapeFocusViewBeginAttemptConsumed
			&& Tool->ScrapeFocusSession.ScrapeFocusGeneration
				== FocusGenerationBeforeActionableEdge + 1
			&& Tool->AuthoritativeAim.NextLocalAimSequence == 0);
	const uint32 FocusGenerationAfterFirstAttempt =
		Tool->ScrapeFocusSession.ScrapeFocusGeneration;
	TestFalse(
		TEXT("The same latch cannot emit a second Begin after an ordinary denial."),
		Tool->TryBeginAuthoritativeScrapeFocusFromCurrentAim());
	TestEqual(
		TEXT("A blocked duplicate leaves the Focus generation unchanged."),
		Tool->ScrapeFocusSession.ScrapeFocusGeneration,
		FocusGenerationAfterFirstAttempt);

	SeedLocalView(12);
	Tool->CurrentScrapeAim.Type = EMD_ScrapeAimQueryType::SurfaceHit;
	Tool->bScrapeFocusViewBeginAttemptConsumed = true;
	Tool->PendingScrapeFocusViewGeneration = 12;
	Tool->ScrapeFocusState = EMD_ScrapeFocusState::FocusPending;
	Tool->ScrapeFocusSession.ActiveScrapeFocusSessionId = 12001;
	Tool->ScrapeFocusSession.ScrapeFocusGeneration = 9;
	TestFalse(
		TEXT("Pending state cannot emit an additional Reliable Begin."),
		Tool->TryBeginAuthoritativeScrapeFocusFromCurrentAim());
	TestTrue(
		TEXT("Pending duplicate suppression preserves session and generation."),
		Tool->GetActiveScrapeFocusSessionId() == 12001
			&& Tool->ScrapeFocusSession.ScrapeFocusGeneration == 9);

	auto SeedPendingReply = [&] (const uint64 SessionId, const uint32 ViewGeneration)
	{
		SeedLocalView(ViewGeneration);
		Tool->ScrapeFocusState = EMD_ScrapeFocusState::FocusPending;
		Tool->ScrapeFocusSession.ActiveScrapeFocusSessionId = SessionId;
		Tool->PendingScrapeFocusViewGeneration = ViewGeneration;
		Tool->bScrapeFocusViewBeginAttemptConsumed = true;
		Tool->ScrapeFocusPointerNormalized = FVector2D(0.3, 0.7);
		Tool->ScrapeVisualContactPose.bValid = true;
		Tool->ScrapeVisualContactPose.UpdateGeneration = 17;
	};

	SeedPendingReply(13001, 13);
	Tool->ClientScrapeFocusBeginResult_Implementation(
		13001,
		EMD_ScrapeFocusBeginResult::Denied,
		FRotator::ZeroRotator,
		90.0f,
		1.0f,
		9.0f / 16.0f);
	FMD_ScrapeVisualContactPose DeniedPose;
	TestTrue(
		TEXT("An ordinary no-target denial clears auth state but preserves the RMB latch."),
		Tool->IsScrapeFocusViewActive()
			&& Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::Free
			&& Tool->GetActiveScrapeFocusSessionId() == 0
			&& !Tool->GetScrapeVisualContactPose(DeniedPose));

	SeedPendingReply(14001, 14);
	Tool->ClientTerminateScrapeFocus_Implementation(
		14001,
		EMD_ScrapeFocusTerminationReason::SessionTimeout);
	TestTrue(
		TEXT("A network timeout returns to targetless view mode without releasing RMB."),
		Tool->IsScrapeFocusViewActive()
			&& Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::Free
			&& Tool->GetActiveScrapeFocusSessionId() == 0);

	SeedPendingReply(15001, 15);
	Tool->ClientTerminateScrapeFocus_Implementation(
		15001,
		EMD_ScrapeFocusTerminationReason::ProtocolViolation);
	TestTrue(
		TEXT("A protocol violation forces the local latch off."),
		!Tool->IsScrapeFocusViewActive()
			&& Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::Free
			&& Tool->GetActiveScrapeFocusSessionId() == 0);

	SeedPendingReply(16001, 16);
	Tool->ClientTerminateScrapeFocus_Implementation(
		16001,
		EMD_ScrapeFocusTerminationReason::EligibilityLost);
	TestTrue(
		TEXT("Eligibility loss forces the local latch off."),
		!Tool->IsScrapeFocusViewActive()
			&& Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::Free);

	SeedPendingReply(17001, 18);
	Tool->PendingScrapeFocusViewGeneration = 17;
	const FRotator RotationBeforeStaleAck = Tool->ScrapeFocusViewRotation;
	Tool->ClientScrapeFocusBeginResult_Implementation(
		17001,
		EMD_ScrapeFocusBeginResult::Accepted,
		FRotator(5.0, 25.0, 0.0),
		75.0f,
		0.8f,
		0.45f);
	TestTrue(
		TEXT("An Ack from an older latch generation cannot mutate the current view."),
		Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::FocusPending
			&& Tool->GetActiveScrapeFocusSessionId() == 17001
			&& Tool->ScrapeFocusViewRotation.Equals(
				RotationBeforeStaleAck,
				0.001f));
	Tool->EndScrapeFocusInternal(false);
	Tool->ClientScrapeFocusBeginResult_Implementation(
		17001,
		EMD_ScrapeFocusBeginResult::Accepted,
		FRotator(5.0, 25.0, 0.0),
		75.0f,
		0.8f,
		0.45f);
	TestTrue(
		TEXT("An Ack arriving after RMB release cannot revive the latch or session."),
		!Tool->IsScrapeFocusViewActive()
			&& Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::Free
			&& Tool->GetActiveScrapeFocusSessionId() == 0);

	SeedLocalView(19);
	Tool->ScrapeFocusPointerNormalized = FVector2D(0.2, 0.8);
	Tool->ScrapeVisualContactPose.bValid = true;
	Tool->EndScrapeFocus();
	TestTrue(
		TEXT("A second RMB-style public end clears targetless view mode."),
		!Tool->IsScrapeFocusViewActive()
			&& Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::Free
			&& Tool->GetScrapeFocusPointerNormalized().Equals(
				FVector2D(0.5, 0.5),
				1.0e-6));

	SeedLocalView(20);
	Tool->TerminateScrapeFocusFromComponent();
	TestTrue(
		TEXT("A component-forced cancel uses the same complete local cleanup."),
		!Tool->IsScrapeFocusViewActive()
			&& Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::Free
			&& Tool->GetActiveScrapeFocusSessionId() == 0);

	TestTrue(
		TEXT("A unit forward InitialAim direction is a valid Begin payload."),
		Tool->IsScrapeFocusBeginPayloadValid(
			20001,
			FRotator::ZeroRotator,
			90.0f,
			16.0f / 9.0f,
			1.0f,
			9.0f / 16.0f,
			FVector::ForwardVector));
	TestFalse(
		TEXT("A non-unit InitialAim direction is rejected rather than normalized."),
		Tool->IsScrapeFocusBeginPayloadValid(
			20002,
			FRotator::ZeroRotator,
			90.0f,
			16.0f / 9.0f,
			1.0f,
			9.0f / 16.0f,
			FVector(0.5, 0.0, 0.0)));
	TestFalse(
		TEXT("A backward InitialAim direction is rejected."),
		Tool->IsScrapeFocusBeginPayloadValid(
			20003,
			FRotator::ZeroRotator,
			90.0f,
			16.0f / 9.0f,
			1.0f,
			9.0f / 16.0f,
			-FVector::ForwardVector));
	FMD_ScrapeAimAuthorityCone BeginCone;
	BeginCone.HorizontalHalfTangent = 1.0f;
	BeginCone.VerticalHalfTangent = 9.0f / 16.0f;
	BeginCone.PointerSafeMargin = 0.05f;
	BeginCone.MarginDegrees = 0.25f;
	const FVector OutsideBeginDirection = FVector(1.0, 2.0, 0.0).GetSafeNormal();
	TestFalse(
		TEXT("A unit InitialAim outside the authority cone is rejected without clamp."),
		FMD_ScrapeAimValidation::IsDirectionInsideCone(
			OutsideBeginDirection,
			BeginCone));
	TestTrue(
		TEXT("Cone validation does not mutate or clamp its input direction."),
		OutsideBeginDirection.Equals(
			FVector(1.0, 2.0, 0.0).GetSafeNormal(),
			1.0e-6));

	FMinimalViewInfo ServerViewInfo;
	if (!TestTrue(
		TEXT("The authority fixture exposes its raw camera view."),
		Tool->TryGetAuthoritativeScrapeView(ServerViewInfo)))
	{
		return false;
	}
	const ULocalPlayer* const LocalPlayerDefaults = GetDefault<ULocalPlayer>();
	const EAspectRatioAxisConstraint AxisConstraint = LocalPlayerDefaults
		? static_cast<EAspectRatioAxisConstraint>(
			LocalPlayerDefaults->AspectRatioAxisConstraint.GetValue())
		: AspectRatio_MaintainYFOV;
	float HorizontalHalfTangent = 0.0f;
	float VerticalHalfTangent = 0.0f;
	if (!TestTrue(
		TEXT("The authority fixture can build the canonical Begin projection."),
		FMD_ScrapeFocusProjection::TryCalculateHalfTangents(
			ServerViewInfo,
			AxisConstraint,
			16.0f / 9.0f,
			UMD_ScrapeToolComponent::IsLegacyMaintainYFOVProjectionEnabled(),
			HorizontalHalfTangent,
			VerticalHalfTangent)))
	{
		return false;
	}

	UStaticMesh* const CubeMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!TestNotNull(TEXT("The server replay fixture loads a collision cube."), CubeMesh))
	{
		return false;
	}
	const FTransform WallTransform(
		ServerViewInfo.Rotation,
		ServerViewInfo.Location + ServerViewInfo.Rotation.Vector() * 180.0,
		FVector(0.1, 4.0, 4.0));
	AMD_CuttableWall* const Wall = World->SpawnActorDeferred<AMD_CuttableWall>(
		AMD_CuttableWall::StaticClass(),
		WallTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	UStaticMeshComponent* const WallMesh = Wall
		? Cast<UStaticMeshComponent>(Wall->GetRootComponent())
		: nullptr;
	if (!TestNotNull(TEXT("The server replay surface can be spawned."), Wall)
		|| !TestNotNull(TEXT("The server replay surface has a mesh root."), WallMesh))
	{
		return false;
	}
	WallMesh->SetStaticMesh(CubeMesh);
	WallMesh->SetCollisionProfileName(TEXT("BlockAll"));
	Wall->FinishSpawning(WallTransform);
	TestTrue(
		TEXT("The authoritative center ray reaches the replay surface."),
		Tool->QueryScrapeAim(
			ServerViewInfo.Location,
			ServerViewInfo.Rotation.Vector()).IsActionable());

	constexpr uint64 AcceptedSessionId = 21001;
	Tool->EndScrapeFocusInternal(false);
	Tool->ServerBeginScrapeFocus_Implementation(
		AcceptedSessionId,
		0,
		ServerViewInfo.Rotation,
		ServerViewInfo.FOV,
		16.0f / 9.0f,
		HorizontalHalfTangent,
		VerticalHalfTangent,
		FVector_NetQuantizeNormal(FVector::ForwardVector));
	TestTrue(
		TEXT("A valid authority Begin adopts its session."),
		Tool->ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
			&& Tool->GetActiveScrapeFocusSessionId() == AcceptedSessionId
			&& Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::FocusedReady);
	const FRotator AcceptedViewRotation =
		Tool->AuthoritativeScrapeFocusViewRotation;
	const uint32 ContinuousSequenceBeforeReplay =
		Tool->AuthoritativeAim.NextLocalAimSequence;
	Tool->ServerBeginScrapeFocus_Implementation(
		AcceptedSessionId,
		0,
		ServerViewInfo.Rotation,
		ServerViewInfo.FOV,
		16.0f / 9.0f,
		HorizontalHalfTangent,
		VerticalHalfTangent,
		FVector_NetQuantizeNormal(FVector::ForwardVector));
	TestTrue(
		TEXT("A same-session Reliable replay re-acks without replacing server state."),
		Tool->ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
			&& Tool->GetActiveScrapeFocusSessionId() == AcceptedSessionId
			&& Tool->AuthoritativeScrapeFocusViewRotation.Equals(
				AcceptedViewRotation,
				0.001f)
			&& Tool->AuthoritativeAim.NextLocalAimSequence
				== ContinuousSequenceBeforeReplay);

	constexpr uint64 ConflictingSessionId = 21002;
	Tool->ServerBeginScrapeFocus_Implementation(
		ConflictingSessionId,
		0,
		ServerViewInfo.Rotation,
		ServerViewInfo.FOV,
		16.0f / 9.0f,
		HorizontalHalfTangent,
		VerticalHalfTangent,
		FVector_NetQuantizeNormal(FVector::ForwardVector));
	TestTrue(
		TEXT("A different Begin cannot replace an already accepted authority session."),
		!Tool->ScrapeFocusSession.bAuthoritativeScrapeFocusAccepted
			&& Tool->GetActiveScrapeFocusSessionId() != ConflictingSessionId
			&& Tool->GetScrapeFocusState() == EMD_ScrapeFocusState::Free);

	return true;
}

#endif
