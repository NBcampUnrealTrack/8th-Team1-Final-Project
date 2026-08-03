// Copyright Epic Games, Inc. All Rights Reserved.

#include "Util/MD_LandscapeLibrary.h"

#if WITH_EDITOR
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeLayerInfoObject.h"
#include "LandscapeProxy.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/IConsoleManager.h"
#include "FileHelpers.h"
#include "UObject/SoftObjectPath.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#endif

ALandscape* UMD_LandscapeLibrary::CreateLandscapeFromR16(
	const FString& R16Path,
	int32 SizeX,
	int32 SizeY,
	int32 SectionSizeQuads,
	int32 SectionsPerComponent,
	int32 ComponentsX,
	int32 ComponentsY,
	FVector Scale,
	FVector Location,
	const FString& ActorLabel)
{
#if WITH_EDITOR
	if (!GEditor)
	{
		UE_LOG(LogTemp, Error, TEXT("MD_LandscapeLibrary: No GEditor"));
		return nullptr;
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("MD_LandscapeLibrary: No editor world"));
		return nullptr;
	}

	const int32 QuadsPerComponent = SectionSizeQuads * SectionsPerComponent;
	const int32 ExpectedX = ComponentsX * QuadsPerComponent + 1;
	const int32 ExpectedY = ComponentsY * QuadsPerComponent + 1;
	if (SizeX != ExpectedX || SizeY != ExpectedY)
	{
		UE_LOG(LogTemp, Error,
			TEXT("MD_LandscapeLibrary: Size %dx%d does not match components %dx%d * section %d * subsections %d (expected %dx%d)"),
			SizeX, SizeY, ComponentsX, ComponentsY, SectionSizeQuads, SectionsPerComponent, ExpectedX, ExpectedY);
		return nullptr;
	}

	TArray<uint8> FileData;
	if (!FFileHelper::LoadFileToArray(FileData, *R16Path))
	{
		UE_LOG(LogTemp, Error, TEXT("MD_LandscapeLibrary: Failed to read %s"), *R16Path);
		return nullptr;
	}

	const int32 NumSamples = SizeX * SizeY;
	const int32 ExpectedBytes = NumSamples * sizeof(uint16);
	if (FileData.Num() < ExpectedBytes)
	{
		UE_LOG(LogTemp, Error, TEXT("MD_LandscapeLibrary: r16 too small (%d < %d)"), FileData.Num(), ExpectedBytes);
		return nullptr;
	}

	TArray<uint16> HeightData;
	HeightData.SetNumUninitialized(NumSamples);
	FMemory::Memcpy(HeightData.GetData(), FileData.GetData(), ExpectedBytes);

	TMap<FGuid, TArray<uint16>> HeightDataPerLayers;
	HeightDataPerLayers.Add(FGuid(), MoveTemp(HeightData));

	TMap<FGuid, TArray<FLandscapeImportLayerInfo>> MaterialLayerDataPerLayers;
	MaterialLayerDataPerLayers.Add(FGuid(), TArray<FLandscapeImportLayerInfo>());

	const FVector Offset = FTransform(FRotator::ZeroRotator, FVector::ZeroVector, Scale)
		.TransformVector(FVector(-ComponentsX * QuadsPerComponent * 0.5, -ComponentsY * QuadsPerComponent * 0.5, 0.0));

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ALandscape* Landscape = World->SpawnActor<ALandscape>(Location + Offset, FRotator::ZeroRotator, SpawnParams);
	if (!Landscape)
	{
		UE_LOG(LogTemp, Error, TEXT("MD_LandscapeLibrary: SpawnActor<ALandscape> failed"));
		return nullptr;
	}

	Landscape->SetActorRelativeScale3D(Scale);
	Landscape->StaticLightingLOD = FMath::DivideAndRoundUp(FMath::CeilLogTwo((SizeX * SizeY) / (2048 * 2048) + 1), (uint32)2);

	Landscape->Import(
		FGuid::NewGuid(),
		0, 0,
		SizeX - 1, SizeY - 1,
		SectionsPerComponent,
		SectionSizeQuads,
		HeightDataPerLayers,
		*R16Path,
		MaterialLayerDataPerLayers,
		ELandscapeImportAlphamapType::Additive,
		TArrayView<const FLandscapeLayer>());

	if (ULandscapeInfo* LandscapeInfo = Landscape->GetLandscapeInfo())
	{
		LandscapeInfo->UpdateLayerInfoMap(Landscape);
	}

	if (!ActorLabel.IsEmpty())
	{
		Landscape->SetActorLabel(ActorLabel);
	}

	UE_LOG(LogTemp, Log, TEXT("MD_LandscapeLibrary: Created landscape '%s' from %s"), *Landscape->GetActorLabel(), *R16Path);
	return Landscape;
#else
	return nullptr;
#endif
}

bool UMD_LandscapeLibrary::RegisterLandscapeTargetLayer(
	ALandscape* Landscape,
	FName LayerName,
	ULandscapeLayerInfoObject* LayerInfo)
{
#if WITH_EDITOR
	if (!Landscape || LayerName.IsNone())
	{
		UE_LOG(LogTemp, Error, TEXT("MD_LandscapeLibrary: RegisterLandscapeTargetLayer invalid args"));
		return false;
	}

	if (LayerInfo)
	{
		LayerInfo->SetLayerName(LayerName, true);
	}

	const FLandscapeTargetLayerSettings Settings(LayerInfo);
	if (Landscape->HasTargetLayer(LayerName))
	{
		Landscape->UpdateTargetLayer(LayerName, Settings, true);
		UE_LOG(LogTemp, Log, TEXT("MD_LandscapeLibrary: Updated target layer '%s'"), *LayerName.ToString());
	}
	else
	{
		Landscape->AddTargetLayer(LayerName, Settings, true);
		UE_LOG(LogTemp, Log, TEXT("MD_LandscapeLibrary: Added target layer '%s'"), *LayerName.ToString());
	}
	return true;
#else
	return false;
#endif
}

int32 UMD_LandscapeLibrary::RegisterLobbyLandscapePaintLayers(ALandscape* Landscape)
{
#if WITH_EDITOR
	if (!Landscape)
	{
		if (GEditor)
		{
			if (UWorld* World = GEditor->GetEditorWorldContext().World())
			{
				for (TActorIterator<ALandscape> It(World); It; ++It)
				{
					Landscape = *It;
					break;
				}
			}
		}
	}
	if (!Landscape)
	{
		UE_LOG(LogTemp, Error, TEXT("MD_LandscapeLibrary: No Landscape for paint layers"));
		return 0;
	}

	static const TCHAR* LayerNames[] = { TEXT("Seabed"), TEXT("Plains"), TEXT("Mountain") };
	int32 Count = 0;
	for (const TCHAR* Name : LayerNames)
	{
		const FString AssetPath = FString::Printf(
			TEXT("/Game/Environment/Lobby/Landscape/Layers/LI_%s.LI_%s"), Name, Name);
		ULandscapeLayerInfoObject* LayerInfo = LoadObject<ULandscapeLayerInfoObject>(nullptr, *AssetPath);
		if (!LayerInfo)
		{
			UE_LOG(LogTemp, Warning, TEXT("MD_LandscapeLibrary: Missing %s"), *AssetPath);
		}
		if (RegisterLandscapeTargetLayer(Landscape, FName(Name), LayerInfo))
		{
			++Count;
		}
	}

	if (ULandscapeInfo* Info = Landscape->GetLandscapeInfo())
	{
		Info->UpdateLayerInfoMap(Landscape);
	}
	Landscape->PostEditChange();
	UE_LOG(LogTemp, Log, TEXT("MD_LandscapeLibrary: Registered %d lobby paint layers on '%s'"),
		Count, *Landscape->GetActorLabel());
	return Count;
#else
	return 0;
#endif
}

#if WITH_EDITOR
static void MDCreateLobbyLandscapeCommand()
{
	const FString LevelPath = TEXT("/Game/Maps/L_Lobby");
	const FString R16Path = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectSavedDir() / TEXT("HM_Lobby_505.r16"));

	FEditorFileUtils::LoadMap(LevelPath);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (World)
	{
		TArray<AActor*> ToDestroy;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* Actor = *It;
			if (!Actor)
			{
				continue;
			}
			const FString Label = Actor->GetActorLabel();
			const FString ClassName = Actor->GetClass()->GetName();
			bool bRemove = Label.StartsWith(TEXT("LobbyTerrain")) || Label == TEXT("LobbyLandscape") || ClassName == TEXT("Landscape");
			if (!bRemove)
			{
				if (UStaticMeshComponent* Mesh = Actor->FindComponentByClass<UStaticMeshComponent>())
				{
					if (UStaticMesh* SM = Mesh->GetStaticMesh())
					{
						bRemove = SM->GetName().Contains(TEXT("LobbyTerrain"));
					}
				}
			}
			if (bRemove)
			{
				ToDestroy.Add(Actor);
			}
		}
		for (AActor* Actor : ToDestroy)
		{
			World->EditorDestroyActor(Actor, true);
		}
	}

	ALandscape* Landscape = UMD_LandscapeLibrary::CreateLandscapeFromR16(
		R16Path,
		505, 505,
		63, 1,
		8, 8,
		FVector(100.0f, 100.0f, 100.0f),
		FVector::ZeroVector,
		TEXT("LobbyLandscape"));

	if (Landscape)
	{
		Landscape->SetFolderPath(TEXT("Terrain"));
		FEditorFileUtils::SaveCurrentLevel();
		UE_LOG(LogTemp, Log, TEXT("MD.CreateLobbyLandscape: SUCCESS"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("MD.CreateLobbyLandscape: FAILED (r16=%s)"), *R16Path);
	}
}

static FAutoConsoleCommand GMDCreateLobbyLandscapeCmd(
	TEXT("MD.CreateLobbyLandscape"),
	TEXT("Replace lobby heightmap mesh with a real Landscape from Saved/HM_Lobby_505.r16"),
	FConsoleCommandDelegate::CreateStatic(&MDCreateLobbyLandscapeCommand));

static void MDRegisterLobbyPaintLayersCommand()
{
	const int32 Count = UMD_LandscapeLibrary::RegisterLobbyLandscapePaintLayers(nullptr);
	if (Count > 0)
	{
		FEditorFileUtils::SaveCurrentLevel();
		UE_LOG(LogTemp, Log, TEXT("MD.RegisterLobbyPaintLayers: SUCCESS (%d)"), Count);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("MD.RegisterLobbyPaintLayers: FAILED"));
	}
}

static FAutoConsoleCommand GMDRegisterLobbyPaintLayersCmd(
	TEXT("MD.RegisterLobbyPaintLayers"),
	TEXT("Add Seabed/Plains/Mountain Landscape paint Target Layers for LobbyLandscape"),
	FConsoleCommandDelegate::CreateStatic(&MDRegisterLobbyPaintLayersCommand));
#endif
