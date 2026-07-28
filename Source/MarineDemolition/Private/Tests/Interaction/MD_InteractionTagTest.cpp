// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "GameplayTags/MD_GameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Sample/MD_SampleItem.h"
#include "Wall/MD_WallPiece.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMD_InteractionClassificationTagsTest,
	"MarineDemolition.Interaction.ClassificationTags",
	EAutomationTestFlags::EditorContext
		| EAutomationTestFlags::EngineFilter)

bool FMD_InteractionClassificationTagsTest::RunTest(
	const FString& Parameters)
{
	const AMD_SampleItem* SampleDefault = GetDefault<AMD_SampleItem>();
	TestNotNull(TEXT("샘플 기본 객체가 존재해야 한다."), SampleDefault);

	FGameplayTagContainer SampleTags;
	SampleDefault->GetOwnedGameplayTags(SampleTags);

	TestTrue(
		TEXT("샘플은 인벤토리 픽업 대상으로 분류되어야 한다."),
		SampleTags.HasTagExact(MDGameplayTags::Interaction_Pickup_Inventory));
	TestFalse(
		TEXT("샘플은 물리 픽업 대상으로 동시에 분류되면 안 된다."),
		SampleTags.HasTagExact(MDGameplayTags::Interaction_Pickup_Physics));
	TestTrue(
		TEXT("샘플 타입 태그가 있어야 한다."),
		SampleTags.HasTagExact(MDGameplayTags::Item_Type_Sample));

	const AMD_WallPiece* WallPieceDefault = GetDefault<AMD_WallPiece>();
	TestNotNull(TEXT("벽 조각 기본 객체가 존재해야 한다."), WallPieceDefault);

	FGameplayTagContainer WallPieceTags;
	WallPieceDefault->GetOwnedGameplayTags(WallPieceTags);

	TestTrue(
		TEXT("벽 조각은 물리 픽업 대상으로 분류되어야 한다."),
		WallPieceTags.HasTagExact(MDGameplayTags::Interaction_Pickup_Physics));
	TestFalse(
		TEXT("벽 조각은 인벤토리 픽업 대상으로 동시에 분류되면 안 된다."),
		WallPieceTags.HasTagExact(MDGameplayTags::Interaction_Pickup_Inventory));
	TestTrue(
		TEXT("벽 조각 타입 태그가 있어야 한다."),
		WallPieceTags.HasTagExact(MDGameplayTags::Item_Type_WallPiece));
	TestFalse(
		TEXT("기본 벽 조각은 오염 상태가 아니어야 한다."),
		WallPieceTags.HasTagExact(MDGameplayTags::Item_State_Contaminated));

	return true;
}

#endif
