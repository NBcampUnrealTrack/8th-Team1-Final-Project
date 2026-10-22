#include "MDMultiUserSafety.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/World.h"
#include "Engine/Brush.h"
#include "Editor/GroupActor.h"
#include "Engine/LevelScriptActor.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/DefaultPhysicsVolume.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "IMultiUserClientModule.h"
#include "IConcertSyncClient.h"
#include "IConcertClient.h"
#include "IConcertSession.h"
#include "IConcertClientWorkspace.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeExit.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/ObjectSaveContext.h"
#include "Misc/PackageName.h"
#include "Dom/JsonObject.h"
#include "UObject/UObjectGlobals.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "String/ParseLines.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace MDSafety
{
    struct FActorExportIndex
    {
        int32 Count = 0;
        TSet<FString> Names;
    };
    FActorExportIndex IndexActorExport(FStringView Text)
    {
        FActorExportIndex Index;
        UE::String::ParseLines(Text, [&Index](FStringView Line)
        {
            Line = Line.TrimStart();
            if (!Line.StartsWith(TEXT("Begin Actor "))) return;
            ++Index.Count;
            // Only copy the small actor header, never the complete export or its property lines.
            FString Name;
            if (FParse::Value(*FString(Line), TEXT("Name="), Name)) Index.Names.Add(MoveTemp(Name));
        });
        return Index;
    }
    FString Digest(const FString& Text)
    {
        FTCHARToUTF8 Bytes(*Text); uint8 Hash[20];
        FSHA1::HashBuffer(Bytes.Get(), Bytes.Length(), Hash);
        return BytesToHex(Hash, 20);
    }
    FString Key(UWorld* World) { return Digest(World->GetOutermost()->GetName()); }
    TArray<AActor*> Actors(UWorld* World)
    {
        TArray<AActor*> Out;
        for (TActorIterator<AActor> It(World); It; ++It)
            if (IsValid(*It) && !It->HasAnyFlags(RF_Transient) && It->ShouldExport() && !It->IsA<ADefaultPhysicsVolume>() && !It->IsA<ALevelScriptActor>() && !It->IsA<AWorldSettings>() && !It->IsActorBeingDestroyed()
                && !(Cast<ABrush>(*It) && Cast<ABrush>(*It)->IsNotForClientOrServer())) Out.Add(*It);
        return Out;
    }
    TSharedRef<FJsonObject> ActorIdentity(const AActor* Actor)
    {
        auto Row = MakeShared<FJsonObject>();
        Row->SetStringField(TEXT("guid"), Actor->GetActorGuid().ToString());
        Row->SetStringField(TEXT("name"), Actor->GetName());
        Row->SetStringField(TEXT("class"), Actor->GetClass()->GetPathName());
        return Row;
    }
    TArray<TSharedPtr<FJsonValue>> GroupRecords(const TArray<AActor*>& AllActors)
    {
        TArray<TSharedPtr<FJsonValue>> Records;
        for (const AActor* Actor : AllActors)
        {
            const AGroupActor* Group = Cast<AGroupActor>(Actor);
            if (!Group) continue;
            auto Row = ActorIdentity(Group);
            Row->SetBoolField(TEXT("locked"), Group->IsLocked());
            TArray<TSharedPtr<FJsonValue>> Members, Children;
            for (const auto& Member : Group->GroupActors)
                if (IsValid(Member)) Members.Add(MakeShared<FJsonValueObject>(ActorIdentity(Member)));
            for (const auto& Child : Group->SubGroups)
                if (IsValid(Child)) Children.Add(MakeShared<FJsonValueObject>(ActorIdentity(Child)));
            Row->SetArrayField(TEXT("members"), Members);
            Row->SetArrayField(TEXT("subgroups"), Children);
            Records.Add(MakeShared<FJsonValueObject>(Row));
        }
        return Records;
    }
    bool ValidIdentity(const TSharedPtr<FJsonValue>& Value)
    {
        if (!Value || Value->Type != EJson::Object) return false;
        const auto Row = Value->AsObject();
        FString Id, Name;
        FGuid Guid;
        return Row->TryGetStringField(TEXT("guid"), Id) && FGuid::Parse(Id, Guid)
            && Row->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty();
    }
    bool ValidateGroups(const TSharedPtr<FJsonObject>& Meta)
    {
        // Old checkpoints have no group metadata and keep their existing recovery format.
        if (!Meta->HasField(TEXT("groups")) && !Meta->HasField(TEXT("group_count"))) return true;
        if (!Meta->HasTypedField<EJson::Array>(TEXT("groups")) || !Meta->HasTypedField<EJson::Number>(TEXT("group_count"))) return false;
        const auto& Groups = Meta->GetArrayField(TEXT("groups"));
        if (Groups.Num() != Meta->GetNumberField(TEXT("group_count"))) return false;
        TSet<FString> Ids;
        for (const auto& Value : Groups)
        {
            if (!ValidIdentity(Value)) return false;
            const auto Row = Value->AsObject();
            const FString Id = Row->GetStringField(TEXT("guid"));
            if (Ids.Contains(Id) || !Row->HasTypedField<EJson::Boolean>(TEXT("locked"))
                || !Row->HasTypedField<EJson::Array>(TEXT("members")) || !Row->HasTypedField<EJson::Array>(TEXT("subgroups"))) return false;
            Ids.Add(Id);
            for (const TCHAR* Field : {TEXT("members"), TEXT("subgroups")})
                for (const auto& Member : Row->GetArrayField(Field)) if (!ValidIdentity(Member)) return false;
        }
        return true;
    }
    bool WriteJson(const FString& Path, const TSharedRef<FJsonObject>& Json)
    {
        FString Text;
        if (!FJsonSerializer::Serialize(Json, TJsonWriterFactory<>::Create(&Text))) return false;
        return FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    }
    TSharedPtr<FJsonObject> ReadJson(const FString& Path)
    {
        FString Text; TSharedPtr<FJsonObject> Json;
        if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json)) return nullptr;
        return Json;
    }
    FString Latest(const FString& Root, UWorld* World)
    {
        auto Head = ReadJson(Root / TEXT("Heads") / (Key(World) + TEXT(".json")));
        FString Id;
        if (!Head || !Head->TryGetStringField(TEXT("id"), Id) || Id.Contains(TEXT("/")) || Id.Contains(TEXT("\\")) || Id.Contains(TEXT(".."))) return {};
        return Root / TEXT("Checkpoints") / Id;
    }
    bool Validate(const FString& Directory, UWorld* World, FString& Error)
    {
        auto Meta = ReadJson(Directory / TEXT("manifest.json")); FString Text;
        if (Directory.IsEmpty() || !IFileManager::Get().FileExists(*(Directory / TEXT("complete"))) || !Meta
            || !Meta->HasTypedField<EJson::String>(TEXT("map")) || !Meta->HasTypedField<EJson::String>(TEXT("sha1"))
            || !Meta->HasTypedField<EJson::Array>(TEXT("actors"))
            || !Meta->HasTypedField<EJson::Number>(TEXT("actor_count"))
            || Meta->GetStringField(TEXT("map")) != World->GetOutermost()->GetName()
            || !FFileHelper::LoadFileToString(Text, *(Directory / TEXT("actors.t3d")))
            || Digest(Text) != Meta->GetStringField(TEXT("sha1")))
        { Error = TEXT("체크포인트가 미완성·손상되었거나 다른 맵의 기록입니다. 원본은 변경하지 않았습니다."); return false; }
        const auto& Rows = Meta->GetArrayField(TEXT("actors"));
        int32 Count = 0;
        UE::String::ParseLines(FStringView(Text), [&Count](FStringView Line)
        { if (Line.TrimStart().StartsWith(TEXT("Begin Actor "))) ++Count; });
        if (Count != Rows.Num() || Count != Meta->GetNumberField(TEXT("actor_count")) || Count == 0)
        { Error = TEXT("체크포인트 액터 수 불일치."); return false; }
        for (const auto& Row : Rows)
            if (!Row || Row->Type != EJson::Object || !Row->AsObject()->HasTypedField<EJson::String>(TEXT("guid"))
                || !Row->AsObject()->HasTypedField<EJson::String>(TEXT("name")))
            { Error = TEXT("액터 색인이 손상되었습니다. 원본은 유지됩니다."); return false; }
        if (!ValidateGroups(Meta))
        { Error = TEXT("그룹 액터 색인이 손상되었습니다. 원본은 유지됩니다."); return false; }
        if (Meta->HasTypedField<EJson::String>(TEXT("base_sha1")))
        {
            TArray<uint8> Data; uint8 Hash[20];
            if (!FFileHelper::LoadFileToArray(Data, *(Directory / TEXT("base.umap"))))
            { Error = TEXT("기본 맵 백업이 없습니다."); return false; }
            FSHA1::HashBuffer(Data.GetData(), Data.Num(), Hash);
            if (BytesToHex(Hash, 20) != Meta->GetStringField(TEXT("base_sha1")))
            { Error = TEXT("기본 맵 백업 체크섬 불일치."); return false; }
        }
        return true;
    }
    bool Git(const FString& Arguments, FString& Output, int32& Code)
    {
        FString Errors;
        return FPlatformProcess::ExecProcess(TEXT("git"), *(TEXT("-C \"") + FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()) + TEXT("\" ") + Arguments), &Code, &Output, &Errors);
    }
}

FMDMultiUserSafety& FMDMultiUserSafety::Get() { static FMDMultiUserSafety Instance; return Instance; }
FString FMDMultiUserSafety::RecoveryRoot() const
{
    FString Root=FPaths::ProjectSavedDir()/TEXT("MultiUserRecovery");
    if (FParse::Param(FCommandLine::Get(),TEXT("MDMultiUserSafetyTest")))
        Root=Root/TEXT("Automation")/FString::FromInt(FPlatformProcess::GetCurrentProcessId());
    return FPaths::ConvertRelativePathToFull(Root);
}
bool FMDMultiUserSafety::Connected() const
{
    if (!IMultiUserClientModule::IsAvailable()) return false;
    const auto Client = IMultiUserClientModule::Get().GetClient()->GetConcertClient();
    return Client->GetSessionConnectionStatus() == EConcertConnectionStatus::Connected;
}
void FMDMultiUserSafety::Start()
{
    if (TickHandle.IsValid() || IsRunningCommandlet()) return;
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FMDMultiUserSafety::Tick), 1.f);
    DragHandle = FEditorDelegates::OnAssetDragStarted.AddRaw(this, &FMDMultiUserSafety::AssetDrag);
    if (IMultiUserClientModule::IsAvailable())
    {
        const auto Client = IMultiUserClientModule::Get().GetClient();
        SyncClient = Client;
        WorkspaceHandle = Client->OnWorkspaceStartup().AddRaw(this, &FMDMultiUserSafety::BindWorkspace);
        // An existing workspace was connected before this module loaded; wait conservatively.
        if (Client->GetWorkspace()) BindWorkspace(Client->GetWorkspace());
    }
    SaveHandle = FCoreUObjectDelegates::OnObjectPreSave.AddRaw(this, &FMDMultiUserSafety::PreSave);
}
void FMDMultiUserSafety::BindWorkspace(const TSharedPtr<IConcertClientWorkspace>& NewWorkspace)
{
    if (const auto Previous = Workspace.Pin()) Previous->OnFinalizeWorkspaceSyncCompleted().Remove(FinalizeHandle);
    Workspace = NewWorkspace;
    bSyncReady = false;
    if (NewWorkspace) FinalizeHandle = NewWorkspace->OnFinalizeWorkspaceSyncCompleted().AddLambda([this] {
        bSyncReady = true;
        StableAfter = FPlatformTime::Seconds() + 30.;
        bCheckedRecovery = false;
    });
}
void FMDMultiUserSafety::Stop()
{
    if (const auto Client = SyncClient.Pin()) Client->OnWorkspaceStartup().Remove(WorkspaceHandle);
    if (const auto Current = Workspace.Pin()) Current->OnFinalizeWorkspaceSyncCompleted().Remove(FinalizeHandle);
    Workspace.Reset(); SyncClient.Reset();
    FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset();
    FEditorDelegates::OnAssetDragStarted.Remove(DragHandle);
    FCoreUObjectDelegates::OnObjectPreSave.Remove(SaveHandle);
}
void FMDMultiUserSafety::Warn(const FString& Message)
{
    UE_LOG(LogTemp, Warning, TEXT("[MDMultiUser Safety] %s"), *Message);
    if (FPlatformTime::Seconds() - LastWarning < 15) return;
    LastWarning = FPlatformTime::Seconds();
    FNotificationInfo Info(FText::FromString(Message)); Info.ExpireDuration = 15.f;
    FSlateNotificationManager::Get().AddNotification(Info);
}
FText FMDMultiUserSafety::Status() const
{
    if (Connected() && !bSyncReady) return FText::FromString(TEXT("체크포인트: 동기화 완료 대기 중"));
    return FText::FromString(LastStatus.IsEmpty() ? TEXT("체크포인트: 수동 실행 · 원본 저장/Persist 없음") : LastStatus);
}

bool FMDMultiUserSafety::Checkpoint(UWorld* World, FString& Error)
{
    Error.Reset();
    ON_SCOPE_EXIT { if (!Error.IsEmpty()) LastStatus = Error; };
    if (bWriting || !World || !GUnrealEd || World->WorldType != EWorldType::Editor || (GEditor && GEditor->PlayWorld))
    { Error = TEXT("PIE·다른 체크포인트 작업 중에는 저장하지 않습니다."); return false; }
    TGuardValue<bool> Writing(bWriting, true);
    const auto AllActors = MDSafety::Actors(World);
    TArray<AActor*> List;
    for (AActor* Actor : AllActors)
        if (!Actor->IsA<AGroupActor>()) List.Add(Actor);
    // CopyActors omits editor grouping objects. Preserve their membership separately instead of
    // treating every grouped map as an incomplete actor export.
    const auto Groups = MDSafety::GroupRecords(AllActors);
    if (List.IsEmpty()) { Error = TEXT("편집 액터 없음 · 이전 체크포인트 유지"); return false; }
    const bool bDirtyBefore = World->GetOutermost()->IsDirty();
    FString Text;
    GUnrealEd->CopyActors(List, World, &Text); // Explicit output: does not alter clipboard or selection.
    const MDSafety::FActorExportIndex Index = MDSafety::IndexActorExport(FStringView(Text));
    const int32 Exported = Index.Count;
    if (Exported != List.Num() || World->GetOutermost()->IsDirty() != bDirtyBefore)
    {
        FString Missing;
        for (auto* Actor : List)
        {
            if (!Index.Names.Contains(Actor->GetName()))
                Missing += Actor->GetClass()->GetPathName() + TEXT(":") + Actor->GetName() + TEXT(" ");
            if (Missing.Len() >= 700) break;
        }
        Error = FString::Printf(TEXT("액터 내보내기 %d/%d · dirty %d->%d · %s. 이전 정상 체크포인트를 유지합니다."), Exported, List.Num(), bDirtyBefore, World->GetOutermost()->IsDirty(), *Missing.Left(700));
        return false;
    }
    const FString Id = FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")) + TEXT("-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Root = RecoveryRoot(), Directory = Root / TEXT("Checkpoints") / Id;
    IFileManager::Get().MakeDirectory(*Directory, true);
    auto Meta = MakeShared<FJsonObject>();
    Meta->SetStringField(TEXT("map"), World->GetOutermost()->GetName());
    Meta->SetStringField(TEXT("sha1"), MDSafety::Digest(Text));
    Meta->SetStringField(TEXT("utc"), FDateTime::UtcNow().ToIso8601());
    Meta->SetNumberField(TEXT("actor_count"), List.Num());
    Meta->SetNumberField(TEXT("group_count"), Groups.Num());
    Meta->SetArrayField(TEXT("groups"), Groups);
    TArray<TSharedPtr<FJsonValue>> Rows;
    for (auto* Actor : List)
    {
        Rows.Add(MakeShared<FJsonValueObject>(MDSafety::ActorIdentity(Actor)));
    }
    Meta->SetArrayField(TEXT("actors"), Rows);
    FString MapFile;
    if (FPackageName::DoesPackageExist(World->GetOutermost()->GetName(), &MapFile))
    {
        if (IFileManager::Get().Copy(*(Directory / TEXT("base.umap")), *MapFile) != COPY_OK)
        { Error = TEXT("원본 맵 백업 실패. 정상 체크포인트를 교체하지 않습니다."); return false; }
    }
    if (!MapFile.IsEmpty())
    {
        TArray<uint8> Data; uint8 Hash[20];
        if (!FFileHelper::LoadFileToArray(Data, *(Directory / TEXT("base.umap")))) return false;
        FSHA1::HashBuffer(Data.GetData(), Data.Num(), Hash);
        Meta->SetStringField(TEXT("base_sha1"), BytesToHex(Hash, 20));
    }
    FString GitHead; int32 Code = -1;
    if (MDSafety::Git(TEXT("rev-parse HEAD"), GitHead, Code) && Code == 0) Meta->SetStringField(TEXT("git_head"), GitHead.TrimStartAndEnd());
    if (!FFileHelper::SaveStringToFile(Text, *(Directory / TEXT("actors.t3d")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
        || !MDSafety::WriteJson(Directory / TEXT("manifest.json"), Meta)
        || !FFileHelper::SaveStringToFile(TEXT("complete"), *(Directory / TEXT("complete"))))
    { Error = TEXT("체크포인트 기록 실패. 원본·이전 기록을 유지합니다."); return false; }
    if (!MDSafety::Validate(Directory, World, Error)) return false;
    const FString Heads = Root / TEXT("Heads"); IFileManager::Get().MakeDirectory(*Heads, true);
    auto Head = MakeShared<FJsonObject>(); Head->SetStringField(TEXT("id"), Id);
    const FString Target = Heads / (MDSafety::Key(World) + TEXT(".json")), Temp = Target + TEXT(".") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
    if (!MDSafety::WriteJson(Temp, Head) || !IFileManager::Get().Move(*Target, *Temp, true, true))
    { Error = TEXT("체크포인트 색인 기록 실패. 원본과 체크포인트 파일은 유지됩니다."); return false; }
    LastStatus = FString::Printf(TEXT("수동 체크포인트 %s · %d 액터 · %d 그룹"), *FDateTime::Now().ToString(TEXT("%H:%M:%S")), List.Num(), Groups.Num());
    return true;
}

bool FMDMultiUserSafety::PrepareRecovery(UWorld* World, FString& Directory, FString& Error)
{
    Error.Reset();
    if (!World) { Error = TEXT("열린 맵이 없습니다."); return false; }
    const FString Source = MDSafety::Latest(RecoveryRoot(), World);
    if (!MDSafety::Validate(Source, World, Error)) return false;
    Directory = RecoveryRoot() / TEXT("Recovered") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    IFileManager::Get().MakeDirectory(*Directory, true);
    for (const TCHAR* File : {TEXT("manifest.json"), TEXT("actors.t3d"), TEXT("base.umap")})
    {
        if (!IFileManager::Get().FileExists(*(Source / File))) continue;
        if (IFileManager::Get().Copy(*(Directory / File), *(Source / File)) != COPY_OK)
        { Error = TEXT("복구본 복사 실패. 원본 세션에는 변경을 적용하지 않았습니다."); return false; }
    }
    if (!FFileHelper::SaveStringToFile(TEXT("complete"), *(Directory / TEXT("complete")))
        || !MDSafety::Validate(Directory, World, Error)) return false;
    LastRecoverySource = Source;
    LastStatus = TEXT("자동복구본 준비됨 · 원본 세션 유지 · 복구 폴더에서 비교 후 반영");
    return true;
}
void FMDMultiUserSafety::CheckForMissingActors(UWorld* World)
{
    const FString Source = MDSafety::Latest(RecoveryRoot(), World); FString Error;
    if (Source.IsEmpty() || Source == LastRecoverySource || !MDSafety::Validate(Source, World, Error)) return;
    const auto Meta = MDSafety::ReadJson(Source / TEXT("manifest.json"));
    TSet<FString> Current, Generated;
    for (TActorIterator<AActor> It(World);It;++It)
        if (IsValid(*It) && It->HasAnyFlags(RF_Transient)) Generated.Add(It->GetClass()->GetPathName()+TEXT(":")+It->GetName());
    for (auto* Actor : MDSafety::Actors(World)) Current.Add(Actor->GetActorGuid().ToString());
    int32 Missing = 0;
    for (const auto& Value : Meta->GetArrayField(TEXT("actors")))
    {
        const auto Row=Value->AsObject(); FString Class;
        Row->TryGetStringField(TEXT("class"),Class);
        if (!Current.Contains(Row->GetStringField(TEXT("guid"))) && !Generated.Contains(Class+TEXT(":")+Row->GetStringField(TEXT("name")))) ++Missing;
    }
    if (Meta->HasTypedField<EJson::Array>(TEXT("groups")))
        for (const auto& Value : Meta->GetArrayField(TEXT("groups")))
            if (!Current.Contains(Value->AsObject()->GetStringField(TEXT("guid")))) ++Missing;
    if (Missing > 0)
    {
        FString Folder;
        if (PrepareRecovery(World, Folder, Error)) Warn(FString::Printf(TEXT("지난 체크포인트의 액터 %d개가 없습니다. 별도 복구본을 자동 준비했습니다. 정상 삭제일 수도 있어 현재 맵에 자동 삽입하지 않습니다."), Missing));
    }
}
bool FMDMultiUserSafety::Tick(float)
{
    if (!GEditor || GEditor->PlayWorld || bWriting) return true;
    UWorld* World = GEditor->GetEditorWorldContext().World();
    const double Now = FPlatformTime::Seconds();
    const bool bConnected = Connected();
    if (ObservedWorld.Get() != World || bConnected != bWasConnected)
    {
        ObservedWorld = World;
        StableAfter = Now + 30.; // Do not compare during initial map/session synchronization.
        bWasConnected = bConnected;
        bCheckedPeers = false;
        bCheckedRecovery = false;
    }
    if (!World || Now < StableAfter || (bConnected && !bSyncReady)) return true;
    if (bConnected && !bCheckedPeers)
    {
        const auto Session = IMultiUserClientModule::Get().GetClient()->GetConcertClient()->GetCurrentSession();
        if (Session)
        {
            const auto& Local = Session->GetLocalClientInfo();
            for (const auto& Peer : Session->GetSessionClients())
                if (Peer.ClientInfo.DeviceName == Local.DeviceName && Peer.ClientInfo.UserName == Local.UserName)
                { Warn(TEXT("같은 PC·사용자의 다른 에디터가 세션에 있습니다. 중복 저장 충돌을 피하려면 작업 중인 에디터를 확인하세요. 자동 종료하지 않습니다.")); break; }
        }
        bCheckedPeers = true;
    }
    if (!bCheckedRecovery)
    {
        CheckForMissingActors(World);
        bCheckedRecovery = true;
    }
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDSafetyActorExportIndexTest, "MDMultiUser.Safety.ActorExportIndex",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDSafetyActorExportIndexTest::RunTest(const FString& Parameters)
{
    const auto Index = MDSafety::IndexActorExport(TEXT(
        "Begin Map\r\n   Begin Level\r\n"
        "      Begin Actor Class=/Script/Engine.Actor Name=Lamp_1 Archetype=Default\r\n"
        "         Property=\"Name=Missing_1 \"\r\n      End Actor\r\n"
        "      Begin Actor Name=\"QuotedLamp_2\" Class=/Script/Engine.Actor\n      End Actor"));
    TestEqual(TEXT("Only actor headers are counted"), Index.Count, 2);
    TestTrue(TEXT("Unquoted actor name"), Index.Names.Contains(TEXT("Lamp_1")));
    TestTrue(TEXT("Quoted name with LF and no final newline"), Index.Names.Contains(TEXT("QuotedLamp_2")));
    TestFalse(TEXT("Property text cannot masquerade as an exported actor"), Index.Names.Contains(TEXT("Missing_1")));

    FString Large;
    Large.Reserve(24 * 1024 * 1024);
    const FString Payload = FString::ChrN(2048, TEXT('x'));
    for (int32 I = 0; I < 10000; ++I)
        Large += FString::Printf(TEXT("Begin Actor Name=Lamp_%d Class=/Script/Engine.Actor\nPayload=%s\nEnd Actor\n"), I, *Payload);
    const double Started = FPlatformTime::Seconds();
    const auto Many = MDSafety::IndexActorExport(FStringView(Large));
    int32 Missing = 0;
    for (int32 I = 0; I < 15000; ++I)
        Missing += !Many.Names.Contains(FString::Printf(TEXT("Lamp_%d"), I));
    const double Elapsed = FPlatformTime::Seconds() - Started;
    TestEqual(TEXT("10,000 headers in a large export"), Many.Count, 10000);
    TestEqual(TEXT("5,000 missing actors are identified"), Missing, 5000);
    AddInfo(FString::Printf(TEXT("Indexed %d characters and checked 15,000 names in %.3f seconds"), Large.Len(), Elapsed));
    TestTrue(TEXT("Large-export validation finishes within a generous 5-second budget"), Elapsed < 5.0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDSafetyOceanCheckpointTest, "MDMultiUser.Safety.OceanCheckpointTiming",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDSafetyOceanCheckpointTest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("MDMultiUserSafetyTest")))
    { AddError(TEXT("Run only in an isolated process with -MDMultiUserSafetyTest")); return false; }
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || World->GetOutermost()->GetName() != TEXT("/Game/Maps/L_Ocean"))
    { AddError(TEXT("The isolated process must open /Game/Maps/L_Ocean")); return false; }
    const bool Dirty = World->GetOutermost()->IsDirty();
    const int32 BeforeActors = MDSafety::Actors(World).Num();
    FString Error;
    const double Started = FPlatformTime::Seconds();
    const bool Saved = FMDMultiUserSafety::Get().Checkpoint(World, Error);
    const double Elapsed = FPlatformTime::Seconds() - Started;
    AddInfo(FString::Printf(TEXT("Ocean checkpoint: %d actors, %.3f seconds, saved=%d, %s"), BeforeActors, Elapsed, Saved, *Error));
    TestTrue(TEXT("A real Ocean checkpoint attempt finishes within 30 seconds"), Elapsed < 30.);
    TestEqual(TEXT("Ocean actor count preserved"), MDSafety::Actors(World).Num(), BeforeActors);
    TestEqual(TEXT("Ocean dirty flag preserved"), World->GetOutermost()->IsDirty(), Dirty);
    if (!TestTrue(TEXT("Ocean checkpoint is saved successfully"), Saved)) return false;
    const FString Source = MDSafety::Latest(FMDMultiUserSafety::Get().RecoveryRoot(), World);
    const auto Meta = MDSafety::ReadJson(Source / TEXT("manifest.json"));
    if (!TestTrue(TEXT("Ocean manifest exists"), Meta.IsValid())) return false;
    const int32 Groups = MDSafety::GroupRecords(MDSafety::Actors(World)).Num();
    TestEqual(TEXT("All Ocean groups recorded separately"), Meta->GetIntegerField(TEXT("group_count")), Groups);
    TestEqual(TEXT("Regular actors plus groups cover the world"), Meta->GetIntegerField(TEXT("actor_count")) + Groups, BeforeActors);
    FString Recovery;
    TestTrue(TEXT("Ocean recovery copy validates"), FMDMultiUserSafety::Get().PrepareRecovery(World, Recovery, Error));
    AddInfo(FString::Printf(TEXT("Ocean backup includes %d groups; recovery: %s"), Groups, *Recovery));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMDSafetyGroupCheckpointTest, "MDMultiUser.Safety.GroupCheckpoint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FMDSafetyGroupCheckpointTest::RunTest(const FString& Parameters)
{
    if (!FParse::Param(FCommandLine::Get(), TEXT("MDMultiUserSafetyTest")))
    { AddError(TEXT("Run only in an isolated process with -MDMultiUserSafetyTest")); return false; }
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || World->GetOutermost()->GetName() != TEXT("/Engine/Maps/Entry"))
    { AddError(TEXT("Group fixture requires /Engine/Maps/Entry; never run in a working map")); return false; }
    const bool Dirty = World->GetOutermost()->IsDirty();
    const int32 Before = MDSafety::Actors(World).Num();
    TArray<AActor*> Spawned;
    ON_SCOPE_EXIT
    {
        for (AActor* Actor : Spawned) if (IsValid(Actor)) World->DestroyActor(Actor);
        World->GetOutermost()->SetDirtyFlag(Dirty);
    };
    auto* Parent = World->SpawnActor<AGroupActor>();
    auto* Child = World->SpawnActor<AGroupActor>();
    auto* Member = World->SpawnActor<AActor>();
    auto* NestedMember = World->SpawnActor<AActor>();
    Spawned = { Parent, Child, Member, NestedMember };
    if (!Parent || !Child || !Member || !NestedMember) { AddError(TEXT("Fixture spawn failed")); return false; }
    Parent->Add(*Member);
    Child->Add(*NestedMember);
    Parent->Add(*Child);
    Parent->bLocked = true;
    Child->Unlock();
    const bool FixtureDirty = World->GetOutermost()->IsDirty();
    FString Error, Recovery;
    auto& Safety = FMDMultiUserSafety::Get();
    if (!TestTrue(TEXT("Grouped scene can be checkpointed"), Safety.Checkpoint(World, Error)))
    { AddError(Error); return false; }
    const FString Source = MDSafety::Latest(Safety.RecoveryRoot(), World);
    const auto Meta = MDSafety::ReadJson(Source / TEXT("manifest.json"));
    if (!TestTrue(TEXT("Manifest exists"), Meta.IsValid())) return false;
    TestEqual(TEXT("Both group objects preserved"), Meta->GetIntegerField(TEXT("group_count")), 2);
    TestEqual(TEXT("Export covers every regular actor"), Meta->GetIntegerField(TEXT("actor_count")), Before + 2);
    for (const auto& Value : Meta->GetArrayField(TEXT("groups")))
    {
        const auto Row = Value->AsObject();
        const bool IsParent = Row->GetStringField(TEXT("guid")) == Parent->GetActorGuid().ToString();
        TestEqual(TEXT("Group lock retained"), Row->GetBoolField(TEXT("locked")), IsParent);
        const auto& Members = Row->GetArrayField(TEXT("members"));
        if (TestEqual(TEXT("Direct group member retained"), Members.Num(), 1))
            TestEqual(TEXT("Member identity retained"), Members[0]->AsObject()->GetStringField(TEXT("guid")),
                (IsParent ? Member : NestedMember)->GetActorGuid().ToString());
        const auto& Children = Row->GetArrayField(TEXT("subgroups"));
        TestEqual(TEXT("Subgroup relationship retained"), Children.Num(), IsParent ? 1 : 0);
        if (IsParent && Children.Num() == 1)
            TestEqual(TEXT("Subgroup identity retained"), Children[0]->AsObject()->GetStringField(TEXT("guid")), Child->GetActorGuid().ToString());
    }
    TestTrue(TEXT("Recovery copy validates"), Safety.PrepareRecovery(World, Recovery, Error));
    FString OriginalManifest, CopiedManifest;
    FFileHelper::LoadFileToString(OriginalManifest, *(Source / TEXT("manifest.json")));
    FFileHelper::LoadFileToString(CopiedManifest, *(Recovery / TEXT("manifest.json")));
    TestEqual(TEXT("Recovery retains exact group metadata"), CopiedManifest, OriginalManifest);
    ON_SCOPE_EXIT { FFileHelper::SaveStringToFile(OriginalManifest, *(Source / TEXT("manifest.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM); };
    Meta->SetNumberField(TEXT("group_count"), 999);
    MDSafety::WriteJson(Source / TEXT("manifest.json"), Meta.ToSharedRef());
    TestFalse(TEXT("Corrupt group index is rejected"), Safety.PrepareRecovery(World, Recovery, Error));
    TestFalse(TEXT("Corrupt group diagnostic exists"), Error.IsEmpty());
    Meta->RemoveField(TEXT("groups"));
    Meta->RemoveField(TEXT("group_count"));
    MDSafety::WriteJson(Source / TEXT("manifest.json"), Meta.ToSharedRef());
    TestTrue(TEXT("Legacy checkpoint remains readable"), Safety.PrepareRecovery(World, Recovery, Error));
    FFileHelper::SaveStringToFile(OriginalManifest, *(Source / TEXT("manifest.json")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
    TestTrue(TEXT("Original checkpoint remains usable"), Safety.PrepareRecovery(World, Recovery, Error));
    TestEqual(TEXT("Checkpoint did not change actor count"), MDSafety::Actors(World).Num(), Before + 4);
    TestEqual(TEXT("Checkpoint did not change map dirty flag"), World->GetOutermost()->IsDirty(), FixtureDirty);
    TestTrue(TEXT("Original grouping remains intact"), Parent->GroupActors.Contains(Member) && Parent->SubGroups.Contains(Child)
        && Child->GroupActors.Contains(NestedMember) && Parent->IsLocked() && !Child->IsLocked());
    return true;
}
#endif

void FMDMultiUserSafety::AssetDrag(const TArray<FAssetData>& Assets, UActorFactory*)
{
    if (!Connected()) return;
    TSet<FName> Packages;
    TArray<FName> Queue;
    for (const auto& Asset : Assets) Queue.Add(Asset.PackageName);
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    while (!Queue.IsEmpty() && Packages.Num() < 2048)
    {
        const FName Name = Queue.Pop(EAllowShrinking::No);
        if (Packages.Contains(Name) || !Name.ToString().StartsWith(TEXT("/Game/"))) continue;
        Packages.Add(Name);
        TArray<FName> Dependencies;
        Registry.GetDependencies(Name, Dependencies, UE::AssetRegistry::EDependencyCategory::Package, UE::AssetRegistry::FDependencyQuery(UE::AssetRegistry::EDependencyQuery::Hard));
        Queue.Append(Dependencies);
    }
    FString Output; int32 Code = -1;
    if (!MDSafety::Git(TEXT("rev-parse --verify @{upstream}"), Output, Code) || Code != 0)
    { Warn(TEXT("Git 원격 게시 여부를 확인할 수 없습니다. 에셋과 의존 파일을 Git에 먼저 올리고 팀원이 풀·빌드한 뒤 배치하세요.")); return; }
    TArray<FString> Unsafe;
    TSet<FString> Tracked, Changed;
    if (!MDSafety::Git(TEXT("-c core.quotepath=false ls-files"), Output, Code) || Code != 0)
    { Warn(TEXT("Git 파일 목록 확인 실패. 배치 전에 게시 여부를 확인하세요.")); return; }
    TArray<FString> Lines; Output.ParseIntoArrayLines(Lines);
    for (const auto& Line : Lines) Tracked.Add(Line);
    if (!MDSafety::Git(TEXT("-c core.quotepath=false diff --name-only @{upstream}"), Output, Code) || Code != 0)
    { Warn(TEXT("Git 원격 추적 기준 비교 실패. 배치 전에 게시 여부를 확인하세요.")); return; }
    Lines.Reset(); Output.ParseIntoArrayLines(Lines);
    for (const auto& Line : Lines) Changed.Add(Line);
    for (FName Name : Packages)
    {
        FString File;
        if (!FPackageName::DoesPackageExist(Name.ToString(), &File)) { Unsafe.Add(Name.ToString()); }
        else
        {
            FString Relative = FPaths::ConvertRelativePathToFull(File);
            FPaths::MakePathRelativeTo(Relative, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
            FPaths::NormalizeFilename(Relative);
            UPackage* Loaded = FindPackage(nullptr, *Name.ToString());
            if ((Loaded && Loaded->IsDirty()) || !Tracked.Contains(Relative) || Changed.Contains(Relative)) Unsafe.Add(Name.ToString());
        }
        if (Unsafe.Num() >= 5) break;
    }
    if (!Queue.IsEmpty()) Unsafe.Add(TEXT("의존 파일 검사 범위 초과"));
    if (!Unsafe.IsEmpty()) Warn(TEXT("배치 전 Git 게시가 필요합니다. 에셋과 의존 파일을 먼저 커밋·푸시하고 팀원이 풀·빌드한 뒤 배치하세요.\n") + FString::Join(Unsafe, TEXT("\n")));
}
void FMDMultiUserSafety::PreSave(UObject* Object, FObjectPreSaveContext)
{
    if (!Connected() || bWriting || !Object || Object->GetOutermost()->HasAnyPackageFlags(PKG_ContainsMap)) return;
    const FString Name = Object->GetOutermost()->GetName();
    if (Name.StartsWith(TEXT("/Game/")) && !Name.StartsWith(TEXT("/Game/_AgentScratch/")))
        Warn(TEXT("세션 중 에셋 저장은 배치 여부와 관계없이 다른 참가자에게 전파될 수 있습니다. 세션 밖에서 수정·저장하고 Git에 올린 뒤 함께 배치하세요.\n") + Name);
}
