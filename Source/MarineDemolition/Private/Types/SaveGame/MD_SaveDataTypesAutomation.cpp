// 프로젝트 설정의 저작권 안내 문구를 사용한다.

#include "Types/SaveGame/MD_SaveDataTypes.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Data/Merchant/MD_SiteDefinition.h"
#include "Misc/AutomationTest.h"

// 필드 컨테이너 마이그레이션과 손상 슬롯 격리 규칙을 회귀 검증하기 위한 자동화 테스트
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMD_SaveDataMigrationTest, "MarineDemolition.SaveGame.SchemaMigration", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMD_SaveDataMigrationTest::RunTest(const FString& Parameters)
{
	// 버전 필드가 없던 FInstancedStruct 슬롯을 현재 스키마로 승격하기 위한 입력
	FMD_GameStateData GameStateData;
	GameStateData.ElapsedDay = 2;
	GameStateData.ElapsedSeconds = 1200.0;
	GameStateData.Score = 350;
	GameStateData.ContractCrewCount = 2;

	FMD_MerchantData MerchantData;
	MerchantData.Credits = 725;
	MerchantData.Affinity = 2.5f;
	MerchantData.CurrentDay = 2;
	MerchantData.CurrentSite = TSoftObjectPtr<UMD_SiteDefinition>(FSoftObjectPath(TEXT("/Game/Data/Site/DA_TestSite.DA_TestSite")));

	FMD_WorldData WorldData;
	FMD_GameData LegacyGameData;
	LegacyGameData.UniqueName = TEXT("LegacySlot");
	LegacyGameData.MapPackagePath = TEXT("/Game/Maps/TestMap");
	TestTrue(TEXT("GameState 필드 추가"), LegacyGameData.AddDataField(FMD_GameStateData::DataFieldKey, GameStateData));
	TestTrue(TEXT("상인 필드 추가"), LegacyGameData.AddDataField(FMD_MerchantData::DataFieldKey, MerchantData));
	TestTrue(TEXT("월드 필드 추가"), LegacyGameData.AddDataField(FMD_WorldData::DataFieldKey, WorldData));

	TestTrue(TEXT("초기 슬롯 마이그레이션 성공"), LegacyGameData.MigrateToCurrentVersion());
	TestEqual(TEXT("현재 스키마 버전 적용"), LegacyGameData.SchemaVersion, FMD_GameData::CurrentSchemaVersion);
	TestTrue(TEXT("승격된 슬롯 구조 검증"), LegacyGameData.IsValidForLoad());

	// 지원하지 않는 미래 버전을 현재 실행 파일이 부분 적용하지 않는지 확인하기 위한 입력
	FMD_GameData FutureGameData = LegacyGameData;
	FutureGameData.UniqueName = TEXT("FutureSlot");
	FutureGameData.SchemaVersion = FMD_GameData::CurrentSchemaVersion + 1;
	TestFalse(TEXT("미래 버전 슬롯 거부"), FutureGameData.MigrateToCurrentVersion());

	// 병렬 오염 배열이 손상된 월드 필드를 슬롯 목록에서 격리하는지 확인하기 위한 입력
	FMD_WorldData CorruptedWorldData;
	FMD_WallContaminationRecord CorruptedContamination;
	CorruptedContamination.WallName = TEXT("Wall_A");
	CorruptedContamination.CellIndices.Add(3);
	CorruptedWorldData.WallContaminations.Add(CorruptedContamination);
	FMD_GameData CorruptedGameData = LegacyGameData;
	CorruptedGameData.UniqueName = TEXT("CorruptedSlot");
	CorruptedGameData.AddDataField(FMD_WorldData::DataFieldKey, CorruptedWorldData);

	UMD_SaveGame* SaveGame = NewObject<UMD_SaveGame>();
	SaveGame->AutoSaveList.Add(LegacyGameData);
	SaveGame->AutoSaveList.Add(CorruptedGameData);
	TestTrue(TEXT("세이브 컨테이너 검증 성공"), SaveGame->MigrateAndValidate());
	TestEqual(TEXT("손상 슬롯 격리"), SaveGame->AutoSaveList.Num(), 1);
	TestEqual(TEXT("유효 슬롯 유지"), SaveGame->AutoSaveList[0].UniqueName, FString(TEXT("LegacySlot")));

	return true;
}

#endif
