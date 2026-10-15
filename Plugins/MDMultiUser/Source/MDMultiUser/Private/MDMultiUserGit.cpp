#include "MDMultiUserGit.h"
#include "MDMultiUserSafety.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Components/ActorComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "UnrealEdGlobals.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "IMultiUserClientModule.h"
#include "IConcertSyncClient.h"
#include "IConcertClient.h"
#include "IConcertClientWorkspace.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/App.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UObjectHash.h"
#include "FileHelpers.h"

namespace MDGit
{
    bool Connected()
    {
        return IMultiUserClientModule::IsAvailable() && IMultiUserClientModule::Get().GetClient()->GetConcertClient()->GetSessionConnectionStatus() != EConcertConnectionStatus::Disconnected;
    }
    FString Quote(const FString& Path)
    {
        // Only fixed application/file paths enter the process command line. User text goes in JSON.
        return TEXT("\"") + Path.Replace(TEXT("\""), TEXT("\\\"")) + TEXT("\"");
    }
    FString Relative(const FString& Package)
    {
        FString File;
        if (!FPackageName::DoesPackageExist(Package, &File)) return {};
        File = FPaths::ConvertRelativePathToFull(File);
        FPaths::MakePathRelativeTo(File, *FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
        FPaths::NormalizeFilename(File);
        return File;
    }
}

FMDMultiUserGit& FMDMultiUserGit::Get() { static FMDMultiUserGit Service; return Service; }
void FMDMultiUserGit::Start()
{
    if (IsRunningCommandlet() || Ticker.IsValid()) return;
    PlacedHandle = FEditorDelegates::OnNewActorsPlaced.AddLambda([this](UObject*,const TArray<AActor*>&) { bPlacementDirty = true; RefreshAt = FPlatformTime::Seconds() + 1.; });
    DroppedHandle = FEditorDelegates::OnNewActorsDropped.AddLambda([this](const TArray<UObject*>&,const TArray<AActor*>&) { bPlacementDirty = true; RefreshAt = FPlatformTime::Seconds() + 1.; });
    Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FMDMultiUserGit::Tick), .25f);
}
void FMDMultiUserGit::Stop()
{
    FEditorDelegates::OnNewActorsPlaced.Remove(PlacedHandle);
    FEditorDelegates::OnNewActorsDropped.Remove(DroppedHandle);
    FTSTicker::RemoveTicker(Ticker); Ticker.Reset();
    // Worker is an independent process and writes result.json even if the editor closes.
    // Never terminate an in-flight Git commit/push or reset its index.
    if (Process.IsValid()) FPlatformProcess::CloseProc(Process);
    Process.Reset();
}
FText FMDMultiUserGit::Status() const
{
    return FText::FromString(FString::Printf(TEXT("Git: %s → %s · ↑%d ↓%d · 선택 %d/%d\n%s"),
        *Branch, *Upstream, Ahead, Behind, Selection.Num(), Files.Num(), *Feedback));
}
void FMDMultiUserGit::Select(const FString& Path, bool bSelect)
{
    for (const auto& File : Files) if (File->Path == Path && !File->OtherPath.IsEmpty())
    {
        if (bSelect) { ManualSelection.Add(File->OtherPath); Excluded.Remove(File->OtherPath); Selection.Add(File->OtherPath); }
        else { ManualSelection.Remove(File->OtherPath); Excluded.Add(File->OtherPath); Selection.Remove(File->OtherPath); }
    }
    if (bSelect) { ManualSelection.Add(Path); Excluded.Remove(Path); Selection.Add(Path); }
    else { ManualSelection.Remove(Path); Excluded.Add(Path); Selection.Remove(Path); }
}
void FMDMultiUserGit::AutoSelectPlaced()
{
    if (!GEditor || GEditor->PlayWorld) return;
    UWorld* World = GEditor->GetEditorWorldContext().World();
    if (!World) return;
    TArray<FName> Queue; TSet<FName> Packages;
    Queue.Add(World->GetOutermost()->GetFName());
    TSet<UObject*> Visited;
    TFunction<void(UObject*)> Collect;
    Collect = [&Queue,&Visited,&Collect](UObject* Object)
    {
        if (!Object || Visited.Contains(Object)) return;
        Visited.Add(Object);
        TArray<UObject*> References;
        FReferenceFinder Finder(References, nullptr, false, false, false, false);
        Finder.FindReferences(Object);
        Queue.Add(Object->GetOutermost()->GetFName());
        for (UObject* Ref : References) if (Ref)
        {
            Queue.Add(Ref->GetOutermost()->GetFName());
            if (Ref->IsA<UMaterialInstanceDynamic>()) Collect(Ref);
        }
    };
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        Collect(*It); Collect(It->GetClass()); Collect(It->GetClass()->GetDefaultObject());
        TArray<FSoftObjectPath> Soft;
        It->GetSoftReferencedContentObjects(Soft);
        for (const auto& Path : Soft) Queue.Add(Path.GetLongPackageFName());
        TInlineComponentArray<UActorComponent*> Components(*It);
        for (auto* Component : Components) Collect(Component);
    }
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    while (!Queue.IsEmpty() && Packages.Num() < 20000)
    {
        const FName Package = Queue.Pop(EAllowShrinking::No);
        if (Packages.Contains(Package) || !Package.ToString().StartsWith(TEXT("/Game/"))) continue;
        Packages.Add(Package);
        if (Package == World->GetOutermost()->GetFName()) continue; // Live actors, not stale on-disk map references.
        if (UPackage* Loaded = FindPackage(nullptr, *Package.ToString()))
        {
            TArray<UObject*> Objects;
            GetObjectsWithPackage(Loaded, Objects, EGetObjectsFlags::IncludeNestedObjects);
            for (UObject* Object : Objects) Collect(Object);
        }
        TArray<FName> Dependencies;
        // Include soft as well as hard package references; do not load those assets.
        Registry.GetDependencies(Package, Dependencies, UE::AssetRegistry::EDependencyCategory::Package);
        Queue.Append(Dependencies);
    }
    TSet<FString> Paths;
    int32 MissingFiles = 0;
    for (FName Package : Packages)
    {
        FString File = MDGit::Relative(Package.ToString());
        if (!File.IsEmpty()) Paths.Add(File); else ++MissingFiles;
    }
    PlacedPaths = Paths;
    Selection.Reset();
    for (const auto& Row : Files) if (ManualSelection.Contains(Row->Path)) Selection.Add(Row->Path);
    for (const auto& Row : Files)
        if (Row->bNeedsPublish && Paths.Contains(Row->Path) && !Excluded.Contains(Row->Path) && !Excluded.Contains(Row->OtherPath)) Selection.Add(Row->Path);
    for (const auto& Row : Files) if (Selection.Contains(Row->Path) && !Row->OtherPath.IsEmpty()) Selection.Add(Row->OtherPath);
    if (MissingFiles > 0) Feedback += FString::Printf(TEXT("\n참조 패키지 %d개가 미저장/파일 없음 상태입니다. 파일 저장 후 Git에 포함할 수 있습니다."),MissingFiles);
    if (!Queue.IsEmpty()) Feedback += TEXT("\n참조 범위 초과: 자동 선택 목록을 검토하세요.");
    ++Revision;
}
bool FMDMultiUserGit::CanWrite(const FString& Op, FString& Error) const
{
    if (!GEditor || GEditor->PlayWorld) { Error = TEXT("플레이 종료 후 Git 작업을 진행하세요."); return false; }
    if (Op == TEXT("pull") || Op == TEXT("commit"))
    {
        if (MDGit::Connected()) { Error = TEXT("세션 변경을 보호하고 연결을 종료한 뒤 진행하세요."); return false; }
        TArray<UPackage*> DirtyMaps, DirtyContent;
        FEditorFileUtils::GetDirtyWorldPackages(DirtyMaps);
        FEditorFileUtils::GetDirtyContentPackages(DirtyContent);
        if (!DirtyMaps.IsEmpty() || !DirtyContent.IsEmpty()) { Error = TEXT("미저장 패키지가 있습니다. 저장·Persist 대상부터 확인하세요."); return false; }
    }
    if (Op == TEXT("pull"))
    {
        if (Upstream.IsEmpty()) { Error = TEXT("원격 추적 브랜치를 먼저 설정하세요."); return false; }
        for (const auto& File : Files)
            if (!File->State.IsEmpty()) { Error = TEXT("로컬 변경 파일이 있어 Pull을 보류했습니다. 먼저 커밋하거나 별도 보관하세요."); return false; }
    }
    if (Op == TEXT("commit") && bPlacementDirty) { Error = TEXT("배치 참조 목록 갱신을 기다린 후 선택 파일을 검토하세요."); return false; }
    if (Op == TEXT("commit") && Selection.IsEmpty()) { Error = TEXT("커밋할 파일을 선택하세요."); return false; }
    if (Branch.IsEmpty()) { Error = TEXT("브랜치 정보를 먼저 새로고침하세요."); return false; }
    return true;
}
bool FMDMultiUserGit::Run(const FString& Op, const FString& Message)
{
    if (Busy()) return false;
    if (Op != TEXT("status") && Op != TEXT("fetch") && !CanWrite(Op, Feedback)) return false;
    if (Op == TEXT("commit") && Message.TrimStartAndEnd().IsEmpty()) { Feedback = TEXT("커밋 메시지를 입력하세요."); return false; }
    if (Op == TEXT("pull"))
    {
        FString Error;
        if (!FMDMultiUserSafety::Get().Checkpoint(GEditor->GetEditorWorldContext().World(), Error))
        { Feedback = Error; return false; }
    }
    if (Op == TEXT("status") || Op == TEXT("fetch")) { AutoSelectPlaced(); bPlacementDirty = false; }
    const FString Python = FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Binaries/ThirdParty/Python3/Win64/python.exe"));
    const FString Worker = FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir() / TEXT("MDMultiUser/Resources/git_bridge.py"));
    if (!FPaths::FileExists(Python) || !FPaths::FileExists(Worker)) { Feedback = TEXT("Git 작업 실행 파일이 없습니다."); return false; }
    Job = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("MultiUserGit") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*Job, true);
    auto Request = MakeShared<FJsonObject>();
    Request->SetStringField(TEXT("root"), FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()));
    Request->SetStringField(TEXT("op"), Op);
    Request->SetStringField(TEXT("head"), Head);
    Request->SetStringField(TEXT("branch"), Branch);
    Request->SetStringField(TEXT("message"), Message);
    TArray<TSharedPtr<FJsonValue>> References;
    for (const FString& Ref : PlacedPaths) References.Add(MakeShared<FJsonValueString>(Ref));
    Request->SetArrayField(TEXT("references"), References);
    TArray<TSharedPtr<FJsonValue>> Selected;
    for (const auto& File : Files)
        if (Selection.Contains(File->Path) && (Op != TEXT("commit") || !File->State.IsEmpty())) Selected.Add(MakeShared<FJsonValueString>(File->Path));
    Request->SetArrayField(TEXT("files"), Selected);
    if (Op == TEXT("pull"))
    {
        Request->SetNumberField(TEXT("editor_pid"), FPlatformProcess::GetCurrentProcessId());
        Request->SetStringField(TEXT("editor"), FPlatformProcess::ExecutablePath());
        Request->SetStringField(TEXT("project"), FPaths::ConvertRelativePathToFull(FPaths::GetProjectFilePath()));
        Request->SetStringField(TEXT("engine_root"), FPaths::ConvertRelativePathToFull(FPaths::EngineDir()));
        Request->SetStringField(TEXT("editor_target"), FApp::GetProjectName() + FString(TEXT("Editor")));
        Request->SetStringField(TEXT("map"), GEditor->GetEditorWorldContext().World()->GetOutermost()->GetName());
    }
    FString Json; FJsonSerializer::Serialize(Request, TJsonWriterFactory<>::Create(&Json));
    const FString Path = Job / TEXT("request.json");
    if (!FFileHelper::SaveStringToFile(Json, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) return false;
    Process = FPlatformProcess::CreateProc(*Python, *(MDGit::Quote(Worker) + TEXT(" ") + MDGit::Quote(Path)), true, true, true, nullptr, 0, nullptr, nullptr);
    if (!Process.IsValid()) { Feedback = TEXT("Git 프로세스를 시작하지 못했습니다."); return false; }
    Operation = Op; Feedback = Op + TEXT(" 진행 중…");
    if (Op == TEXT("pull")) GUnrealEd->CloseEditor();
    return true;
}
bool FMDMultiUserGit::Tick(float)
{
    if (Busy())
    {
        const FString Path = Job / TEXT("result.json");
        FString Text; TSharedPtr<FJsonObject> Result;
        if (FFileHelper::LoadFileToString(Text, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Result))
        {
            Feedback = Result->GetStringField(TEXT("message"));
            if (Result->GetBoolField(TEXT("ok")))
            {
                Branch = Result->GetStringField(TEXT("branch")); Head = Result->GetStringField(TEXT("head")); Upstream = Result->GetStringField(TEXT("upstream"));
                Ahead = Result->GetIntegerField(TEXT("ahead")); Behind = Result->GetIntegerField(TEXT("behind")); Files.Reset();
                for (const auto& Value : Result->GetArrayField(TEXT("files")))
                {
                    auto File = MakeShared<FMDGitFile>(); auto Row = Value->AsObject();
                    File->Path = Row->GetStringField(TEXT("path")); File->State = Row->GetStringField(TEXT("state")); File->bUnpublished = Row->GetBoolField(TEXT("unpublished"));
                    Row->TryGetBoolField(TEXT("needs_publish"), File->bNeedsPublish);
                    if (!Row->TryGetStringField(TEXT("rename_from"), File->OtherPath)) Row->TryGetStringField(TEXT("rename_to"), File->OtherPath);
                    Files.Add(File);
                }
                AutoSelectPlaced();
            }
            FPlatformProcess::CloseProc(Process); Process.Reset();
            RefreshAt = FPlatformTime::Seconds() + (bPlacementDirty ? 1. : 30.); ++Revision;
        }
        else if (!FPlatformProcess::IsProcRunning(Process))
        { Feedback = TEXT("Git 결과가 없습니다. Saved/MultiUserGit에서 로그를 확인하세요."); FPlatformProcess::CloseProc(Process); Process.Reset(); RefreshAt = FPlatformTime::Seconds() + 30.; }
    }
    else if (FPlatformTime::Seconds() >= RefreshAt && (!GEditor || !GEditor->PlayWorld)) Run(TEXT("status"));
    return true;
}
