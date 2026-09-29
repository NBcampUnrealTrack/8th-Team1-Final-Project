#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Types/MD_SampleFloatSettings.h"
#include "Types/Persistence/MD_WorldActorSnapshotTypes.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDSampleFloatTest, "MarineDemolition.Sample.WaterFloat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMDSampleFloatTest::RunTest(const FString& Parameters)
{
	FMD_SampleFloatSettings Settings;
	TestTrue(TEXT("Existing samples remain unmodified"), Settings.GetAcceleration(0, FVector::ZeroVector, 0, -980).IsZero());
	Settings.bEnabled = true;
	TestTrue(TEXT("Above water falls normally"), Settings.GetAcceleration(50, FVector::ZeroVector, 0, -980).IsZero());
	TestTrue(TEXT("Deep/inland placement is not pulled through floors"), Settings.GetAcceleration(-200, FVector::ZeroVector, 0, -980).IsZero());
	// Integrate the force at several frame rates: actual gravity plus authored buoyancy.
	for (float Dt : {1.f / 30.f, 1.f / 60.f, 1.f / 120.f})
	{
		float Z = -40.f;
		FVector Velocity(20.f, 0.f, 0.f);
		float MinZ = 1000.f, MaxZ = -1000.f;
		for (float Time = 0; Time < 20.f; Time += Dt)
		{
			Velocity += (Settings.GetAcceleration(Z, Velocity, Time, -980.f) + FVector(0, 0, -980)) * Dt;
			Z += Velocity.Z * Dt;
			if (Time > 10.f) { MinZ = FMath::Min(MinZ, Z); MaxZ = FMath::Max(MaxZ, Z); }
		}
		TestTrue(TEXT("Settles near surface"), MinZ > -8.f && MaxZ < 8.f);
		TestTrue(TEXT("Continues gentle bobbing"), MaxZ - MinZ > 1.f);
		TestTrue(TEXT("Horizontal drift damps"), FMath::Abs(Velocity.X) < 0.1f);
	}
	FMD_QuestItemRecord Before;
	Before.WaterFloat = Settings;
	Before.WaterFloat.SurfaceZ = 125.f;
	Before.WaterFloat.HeightOffset = 7.f;
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	FObjectAndNameAsStringProxyArchive Save(Writer, false);
	FMD_QuestItemRecord::StaticStruct()->SerializeItem(Save, &Before, nullptr);
	FMemoryReader Reader(Bytes);
	FObjectAndNameAsStringProxyArchive Load(Reader, false);
	FMD_QuestItemRecord After;
	FMD_QuestItemRecord::StaticStruct()->SerializeItem(Load, &After, nullptr);
	TestTrue(TEXT("Enabled survives snapshot serialization"), After.WaterFloat.bEnabled);
	TestEqual(TEXT("Surface survives snapshot serialization"), After.WaterFloat.SurfaceZ, 125.f);
	TestEqual(TEXT("Offset survives snapshot serialization"), After.WaterFloat.HeightOffset, 7.f);
	Settings.BobPeriod = 0.f;
	TestFalse(TEXT("Invalid period rejected"), Settings.IsValid());
	TestTrue(TEXT("Invalid settings never apply force"), Settings.GetAcceleration(0, FVector::ZeroVector, 1, -980).IsZero());
	return true;
}
#endif
