#include "Tools/MD_PropPlacementPreview.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

#define LOCTEXT_NAMESPACE "MDPropPlacementPreview"

bool MDCollectPropPreviewParts(
	UClass* ActorClass, TArray<FMD_PropPlacementPreviewPart>& OutParts,
	FVector& OutActorScale, FText& OutError)
{
	OutParts.Reset();
	OutActorScale = FVector::OneVector;
	OutError = FText::GetEmpty();
	if (!IsValid(ActorClass) || !ActorClass->IsChildOf(AActor::StaticClass()))
	{
		OutError = LOCTEXT("MissingClass", "배치할 액터 블루프린트를 선택하세요.");
		return false;
	}

	const AActor* Defaults = ActorClass->GetDefaultObject<AActor>();
	const USceneComponent* Root = Defaults->GetRootComponent();
	UBlueprintGeneratedClass* BlueprintClass = Cast<UBlueprintGeneratedClass>(ActorClass);
	TMap<const USceneComponent*, USCS_Node*> Nodes;
	for (UClass* Current = ActorClass; Current; Current = Current->GetSuperClass())
	{
		const UBlueprintGeneratedClass* CurrentBlueprint = Cast<UBlueprintGeneratedClass>(Current);
		if (!CurrentBlueprint || !CurrentBlueprint->SimpleConstructionScript)
		{
			continue;
		}
		USimpleConstructionScript* SCS = CurrentBlueprint->SimpleConstructionScript;
		if (!Root)
		{
			USCS_Node* RootNode = nullptr;
			Root = SCS->GetSceneRootComponentTemplate(true, &RootNode);
			// The engine helper deliberately omits the auto-created DefaultSceneRoot.
			// Use it only when it belongs to this SCS root set, not an unused child-BP placeholder.
			if (!Root && SCS->GetRootNodes().Contains(SCS->GetDefaultSceneRootNode()))
			{
				RootNode = SCS->GetDefaultSceneRootNode();
			}
			if (RootNode)
			{
				Root = Cast<USceneComponent>(RootNode->GetActualComponentTemplate(BlueprintClass));
			}
		}
		for (USCS_Node* Node : SCS->GetAllNodes())
		{
			if (Node)
			{
				if (const USceneComponent* Component = Cast<USceneComponent>(Node->GetActualComponentTemplate(BlueprintClass)))
				{
					Nodes.Add(Component, Node);
				}
			}
		}
	}
	if (!Root)
	{
		OutError = LOCTEXT("MissingRoot", "기본 루트 컴포넌트를 찾지 못했습니다.\n\n액터의 로컬 좌표와 크기에 맞는 Blueprint Preview Mesh를 직접 지정하세요.");
		return false;
	}
	OutActorScale = Root->GetRelativeScale3D();

	// Resolve template attachment chains without spawning an actor or running construction scripts.
	TMap<const USceneComponent*, FTransform> Transforms;
	TSet<const USceneComponent*> Visiting;
	TFunction<bool(const USceneComponent*, FTransform&)> ResolveTransform;
	ResolveTransform = [&](const USceneComponent* Component, FTransform& OutTransform)
	{
		if (!IsValid(Component) || Visiting.Contains(Component))
		{
			return false;
		}
		if (const FTransform* Cached = Transforms.Find(Component))
		{
			OutTransform = *Cached;
			return true;
		}
		if (Component == Root)
		{
			OutTransform = FTransform::Identity;
			Transforms.Add(Component, OutTransform);
			return true;
		}
		if (Component->IsUsingAbsoluteLocation() || Component->IsUsingAbsoluteRotation() || Component->IsUsingAbsoluteScale())
		{
			return false;
		}

		Visiting.Add(Component);
		const USceneComponent* Parent = Component->GetAttachParent();
		FName Socket = Component->GetAttachSocketName();
		if (USCS_Node* const* NodePtr = Nodes.Find(Component))
		{
			USCS_Node* Node = *NodePtr;
			Socket = Node->AttachToName;
			if (const USCS_Node* ParentNode = Node->GetSCS()->FindParentNode(Node))
			{
				Parent = Cast<USceneComponent>(ParentNode->GetActualComponentTemplate(BlueprintClass));
			}
			else if (!Node->ParentComponentOrVariableName.IsNone())
			{
				Parent = Node->GetParentComponentTemplate(BlueprintClass);
				if (!Parent)
				{
					Visiting.Remove(Component);
					return false;
				}
			}
		}
		if (!Parent)
		{
			Parent = Root;
		}
		FTransform ParentTransform;
		if (!ResolveTransform(Parent, ParentTransform))
		{
			Visiting.Remove(Component);
			return false;
		}
		if (!Socket.IsNone())
		{
			// Static mesh sockets are stable default geometry; animated sockets need an explicit proxy.
			if (!Parent->IsA<UStaticMeshComponent>() || !Parent->DoesSocketExist(Socket))
			{
				Visiting.Remove(Component);
				return false;
			}
			ParentTransform = Parent->GetSocketTransform(Socket, RTS_Component) * ParentTransform;
		}
		OutTransform = Component->GetRelativeTransform() * ParentTransform;
		Visiting.Remove(Component);
		if (!OutTransform.IsValid())
		{
			return false;
		}
		Transforms.Add(Component, OutTransform);
		return true;
	};

	TArray<const UStaticMeshComponent*> Components;
	AActor::GetActorClassDefaultComponents<UStaticMeshComponent>(ActorClass, Components);
	for (const UStaticMeshComponent* Component : Components)
	{
		if (!IsValid(Component->GetStaticMesh()) || !Component->IsVisible() || Component->IsEditorOnly())
		{
			continue;
		}
		FTransform LocalTransform;
		if (Component->IsA<USplineMeshComponent>() || !ResolveTransform(Component, LocalTransform))
		{
			OutParts.Reset();
			OutError = LOCTEXT("UnsupportedTransform", "이 블루프린트는 기본 메시만으로 미리보기를 만들 수 없습니다.\n변형 메시, 절대 좌표 또는 확인할 수 없는 부착 관계가 포함되어 있습니다.\n\n전체 외형을 나타내는 Blueprint Preview Mesh를 직접 지정하세요.");
			return false;
		}
		auto AddPart = [&](const FTransform& Transform)
		{
			FMD_PropPlacementPreviewPart& Part = OutParts.AddDefaulted_GetRef();
			Part.Mesh = Component->GetStaticMesh();
			Part.LocalTransform = Transform;
		};
		if (const UInstancedStaticMeshComponent* Instances = Cast<UInstancedStaticMeshComponent>(Component))
		{
			for (int32 Index = 0; Index < Instances->GetInstanceCount(); ++Index)
			{
				FTransform InstanceTransform;
				if (Instances->GetInstanceTransform(Index, InstanceTransform, false))
				{
					AddPart(InstanceTransform * LocalTransform);
				}
			}
		}
		else
		{
			AddPart(LocalTransform);
		}
	}
	if (OutParts.IsEmpty())
	{
		OutError = LOCTEXT("MissingMeshes", "미리보기에 사용할 기본 스태틱 메시가 없습니다.\n\n컨스트럭션 스크립트 등으로 외형을 만드는 경우, 전체 외형을 나타내는 Blueprint Preview Mesh를 직접 지정하세요.");
		return false;
	}
	return true;
}

#undef LOCTEXT_NAMESPACE
