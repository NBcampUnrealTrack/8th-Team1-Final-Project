// Copyright Epic Games, Inc. All Rights Reserved.

#include "Gameplay/Scrape/MD_ScrapeToolPoseSolver.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Character/MD_Character.h"
#include "Component/Scrape/MD_ScrapeToolComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Types/Scrape/MD_ScrapeToolState.h"

namespace
{
	constexpr double ScrapePoseLocationToleranceCm = 0.001;
	constexpr double ScrapePoseDirectionTolerance = 0.0001;

	bool AreScrapePoseRotationsEquivalent(
		const FQuat& First,
		const FQuat& Second)
	{
		return FMath::Abs(First.GetNormalized() | Second.GetNormalized())
			>= 1.0 - ScrapePoseDirectionTolerance;
	}

	bool AreScrapePoseTransformsEquivalent(
		const FTransform& First,
		const FTransform& Second)
	{
		return First.GetLocation().Equals(
				Second.GetLocation(),
				ScrapePoseLocationToleranceCm)
			&& AreScrapePoseRotationsEquivalent(
				First.GetRotation(),
				Second.GetRotation())
			&& First.GetScale3D().Equals(
				Second.GetScale3D(),
				ScrapePoseDirectionTolerance);
	}

	class FMDScrapePoseTestWorld final
	{
	public:
		FMDScrapePoseTestWorld()
		{
			if (!GEngine)
			{
				return;
			}

			FWorldContext& WorldContext = GEngine->CreateNewWorldContext(
				EWorldType::Game);
			const FName WorldName = MakeUniqueObjectName(
				nullptr,
				UWorld::StaticClass(),
				TEXT("MDScrapePoseTestWorld"),
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
			WorldContext.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
		}

		~FMDScrapePoseTestWorld()
		{
			if (!World)
			{
				return;
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
	FMDScrapeToolPoseSolverCompositionTest,
	"MarineDemolition.Scrape.ToolPoseSolver.ContactComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMDScrapeToolPoseSolverCompositionTest::RunTest(
	const FString& Parameters)
{
	const FTransform BladeContactMeshLocal(
		FRotator(11.0, -23.0, 7.0),
		FVector(18.0, -4.0, 2.5));
	const FTransform ToolRelativeToAttachSocket(
		FRotator(-9.0, 31.0, 14.0),
		FVector(-6.0, 3.0, 1.0),
		FVector(1.1));
	const FTransform ToolAttachSocketParentBoneLocal(
		FRotator(4.0, 17.0, -12.0),
		FVector(2.0, 5.0, -1.0));

	FTransform DesiredContactWorld;
	FVector StableWorldTangent = FVector::ZeroVector;
	const bool bBuiltContact =
		FMD_ScrapeToolPoseSolver::BuildContactWorldTransform(
			FVector(125.0, -42.0, 88.0),
			FVector(1.0, 2.0, 3.0),
			FVector(4.0, -1.0, 0.5),
			FVector::ZeroVector,
			FVector(1.1),
			DesiredContactWorld,
			StableWorldTangent);
	TestTrue(TEXT("A finite contact frame is constructed"), bBuiltContact);
	if (!bBuiltContact)
	{
		return false;
	}

	FMD_ScrapeToolPoseSolution Solution;
	const bool bSolved =
		FMD_ScrapeToolPoseSolver::SolveHandBoneWorldTransform(
			BladeContactMeshLocal,
			ToolRelativeToAttachSocket,
			ToolAttachSocketParentBoneLocal,
			DesiredContactWorld,
			Solution);
	TestTrue(TEXT("The hand transform can be solved"), bSolved);
	if (!bSolved)
	{
		return false;
	}

	const FTransform ReattachedToolWorld = ToolRelativeToAttachSocket
		* ToolAttachSocketParentBoneLocal
		* Solution.HandBoneWorldTransform;
	const FTransform ReconstructedContact = BladeContactMeshLocal
		* ReattachedToolWorld;
	TestTrue(
		TEXT("Blade_Tip recomposes to the desired contact transform"),
		AreScrapePoseTransformsEquivalent(
			ReconstructedContact,
			DesiredContactWorld));
	TestTrue(
		TEXT("The public contact invariant accepts the recomposed tool"),
		FMD_ScrapeToolPoseSolver::IsContactInvariantSatisfied(
			BladeContactMeshLocal,
			ReattachedToolWorld,
			DesiredContactWorld));
	TestTrue(
		TEXT("The contact normal is the contact frame X axis"),
		DesiredContactWorld.GetUnitAxis(EAxis::X).Equals(
			FVector(1.0, 2.0, 3.0).GetSafeNormal(),
			ScrapePoseDirectionTolerance));
	TestTrue(
		TEXT("The stabilized stroke tangent is the contact frame Z axis"),
		DesiredContactWorld.GetUnitAxis(EAxis::Z).Equals(
			StableWorldTangent,
			ScrapePoseDirectionTolerance));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMDScrapeToolPoseSolverReachabilityTest,
	"MarineDemolition.Scrape.ToolPoseSolver.Reachability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMDScrapeToolPoseSolverReachabilityTest::RunTest(
	const FString& Parameters)
{
	const FVector Shoulder = FVector::ZeroVector;
	constexpr double UpperArmLength = 60.0;
	constexpr double LowerArmLength = 40.0;

	TestTrue(
		TEXT("The exact folded minimum is reachable"),
		FMD_ScrapeToolPoseSolver::IsWithinTwoBoneReach(
			Shoulder,
			FVector(20.0, 0.0, 0.0),
			UpperArmLength,
			LowerArmLength,
			1.0));
	TestFalse(
		TEXT("A target inside the folded minimum is rejected"),
		FMD_ScrapeToolPoseSolver::IsWithinTwoBoneReach(
			Shoulder,
			FVector(19.9, 0.0, 0.0),
			UpperArmLength,
			LowerArmLength,
			1.0));
	TestTrue(
		TEXT("The exact physical maximum is reachable"),
		FMD_ScrapeToolPoseSolver::IsWithinTwoBoneReach(
			Shoulder,
			FVector(100.0, 0.0, 0.0),
			UpperArmLength,
			LowerArmLength,
			1.0));
	TestFalse(
		TEXT("A target beyond the physical maximum is rejected"),
		FMD_ScrapeToolPoseSolver::IsWithinTwoBoneReach(
			Shoulder,
			FVector(100.1, 0.0, 0.0),
			UpperArmLength,
			LowerArmLength,
			1.0));
	TestTrue(
		TEXT("A target at the configured reach ratio is accepted"),
		FMD_ScrapeToolPoseSolver::IsWithinTwoBoneReach(
			Shoulder,
			FVector(95.0, 0.0, 0.0),
			UpperArmLength,
			LowerArmLength,
			0.95));
	TestFalse(
		TEXT("A target beyond the configured reach ratio is rejected"),
		FMD_ScrapeToolPoseSolver::IsWithinTwoBoneReach(
			Shoulder,
			FVector(95.1, 0.0, 0.0),
			UpperArmLength,
			LowerArmLength,
			0.95));
	TestFalse(
		TEXT("A ratio above one never enables stretching"),
		FMD_ScrapeToolPoseSolver::IsWithinTwoBoneReach(
			Shoulder,
			FVector(100.1, 0.0, 0.0),
			UpperArmLength,
			LowerArmLength,
			1.5));
	TestFalse(
		TEXT("An invalid zero-length chain fails closed"),
		FMD_ScrapeToolPoseSolver::IsWithinTwoBoneReach(
			Shoulder,
			FVector(10.0, 0.0, 0.0),
			0.0,
			LowerArmLength,
			1.0));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMDScrapeToolPoseSolverTangentIdentityTest,
	"MarineDemolition.Scrape.ToolPoseSolver.TangentIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMDScrapeToolPoseSolverTangentIdentityTest::RunTest(
	const FString& Parameters)
{
	const FVector ContactNormal = FVector::UpVector;
	const FVector CandidateTangent(2.0, 0.0, 1.0);
	const FVector PreviousStableTangent = -FVector::ForwardVector;

	FTransform StableContactWorld;
	FVector StableTangent = FVector::ZeroVector;
	TestTrue(
		TEXT("An opposing but equivalent tangent is accepted"),
		FMD_ScrapeToolPoseSolver::BuildContactWorldTransform(
			FVector::ZeroVector,
			ContactNormal,
			CandidateTangent,
			PreviousStableTangent,
			FVector::OneVector,
			StableContactWorld,
			StableTangent));
	TestTrue(
		TEXT("The tangent is projected into the contact plane"),
		FMath::Abs(FVector::DotProduct(StableTangent, ContactNormal))
			<= ScrapePoseDirectionTolerance);
	TestTrue(
		TEXT("The 180-degree equivalent sign follows the previous identity"),
		FVector::DotProduct(StableTangent, PreviousStableTangent)
			>= 1.0 - ScrapePoseDirectionTolerance);

	FTransform ResetContactWorld;
	FVector ResetTangent = FVector::ZeroVector;
	TestTrue(
		TEXT("Clearing the previous identity accepts a fresh tangent sign"),
		FMD_ScrapeToolPoseSolver::BuildContactWorldTransform(
			FVector::ZeroVector,
			ContactNormal,
			CandidateTangent,
			FVector::ZeroVector,
			FVector::OneVector,
			ResetContactWorld,
			ResetTangent));
	TestTrue(
		TEXT("The fresh identity follows the candidate sign"),
		FVector::DotProduct(
			ResetTangent,
			FVector::ForwardVector)
			>= 1.0 - ScrapePoseDirectionTolerance);

	FMD_ScrapeVisualContactPose Pose;
	Pose.FocusSessionId = 17;
	Pose.LmbStrokeGeneration = 9;
	Pose.UpdateGeneration = 31;
	Pose.bValid = true;
	Pose.bTangentValid = true;
	Pose.bDirectionLocked = true;
	Pose = FMD_ScrapeVisualContactPose();
	TestEqual(TEXT("A reset pose clears its focus identity"), Pose.FocusSessionId, uint64(0));
	TestEqual(TEXT("A reset pose clears its stroke identity"), Pose.LmbStrokeGeneration, uint32(0));
	TestEqual(TEXT("A reset pose reserves update generation zero"), Pose.UpdateGeneration, uint32(0));
	TestFalse(TEXT("A reset pose is invalid"), Pose.bValid);
	TestFalse(TEXT("A reset pose has no tangent"), Pose.bTangentValid);
	TestFalse(TEXT("A reset pose has no direction lock"), Pose.bDirectionLocked);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMDScrapeAnimationInvalidPoseTest,
	"MarineDemolition.Scrape.Animation.InvalidPoseFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMDScrapeAnimationInvalidPoseTest::RunTest(
	const FString& Parameters)
{
	FMDScrapePoseTestWorld TestWorld;
	if (!TestNotNull(TEXT("A transient game world is available"), TestWorld.Get()))
	{
		return false;
	}

	AMD_Character* const Character =
		TestWorld.Get()->SpawnActor<AMD_Character>();
	if (!TestNotNull(TEXT("The scrape character can be spawned"), Character))
	{
		return false;
	}

	UMD_ScrapeToolComponent* const ScrapeComponent =
		Character->GetScrapeToolComponent();
	if (!TestNotNull(TEXT("The scrape component exists"), ScrapeComponent))
	{
		return false;
	}
	FMD_ScrapeVisualContactPose VisualPose;
	TestFalse(
		TEXT("The default visual contact snapshot is invalid"),
		ScrapeComponent->GetScrapeVisualContactPose(VisualPose));
	TestFalse(TEXT("The copied snapshot is marked invalid"), VisualPose.bValid);

	// The public API deliberately has no test-only injection hook for Character's
	// private local visual-pose cache. An actor with no usable local visual pose is
	// therefore the narrow public boundary for verifying fail-closed output.

	FTransform EffectorTransformComponentSpace(
		FRotator(25.0, 10.0, -15.0),
		FVector(40.0, 50.0, 60.0));
	FVector JointTargetLocationComponentSpace(10.0, 20.0, 30.0);
	float Alpha = 1.0f;
	bool bActive = true;
	bool bReachable = true;
	bool bTeleportOrSnap = true;
	int64 PoseGeneration = 99;
	Character->GetScrapeAnimationHandIK(
		1.0 / 60.0,
		EffectorTransformComponentSpace,
		JointTargetLocationComponentSpace,
		Alpha,
		bActive,
		bReachable,
		bTeleportOrSnap,
		PoseGeneration);

	TestEqual(TEXT("An invalid pose outputs zero alpha"), Alpha, 0.0f);
	TestFalse(TEXT("An invalid pose leaves IK inactive"), bActive);
	TestFalse(TEXT("An invalid pose is not reported reachable"), bReachable);
	TestTrue(
		TEXT("An invalid pose clears the effector transform"),
		EffectorTransformComponentSpace.Equals(FTransform::Identity));
	TestTrue(
		TEXT("An invalid pose clears the joint target"),
		JointTargetLocationComponentSpace.IsNearlyZero());
	TestEqual(TEXT("An invalid pose exposes generation zero"), PoseGeneration, int64(0));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
