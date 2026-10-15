#pragma once

#include "IDetailCustomization.h"
#include "Input/Reply.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/StrongObjectPtr.h"

class AMD_SurfaceStain;
class UMD_SurfaceStainDefinition;

class FMD_StainCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	void RefreshSizing() const;
	bool CanEdit() const;
	bool ResolveDraft(int32& OutSize, FVector& OutDimensions, FString& Error) const;
	FText GetDimensionsText() const;
	FText GetDraftText() const;
	FReply RefitDraft();

	TWeakObjectPtr<AMD_SurfaceStain> Stain;
	mutable TSoftObjectPtr<UMD_SurfaceStainDefinition> SourceReference;
	mutable TStrongObjectPtr<UMD_SurfaceStainDefinition> Source;
	mutable TWeakObjectPtr<UMD_SurfaceStainDefinition> ActiveDefinition;
	mutable uint32 SourceRevision = MAX_uint32, ActiveRevision = MAX_uint32;
	mutable int32 CurrentSize = INDEX_NONE;
	mutable FVector OriginalDimensions = FVector::ZeroVector;
	mutable FVector CurrentDimensions = FVector::ZeroVector;
	mutable FVector2D AllowedWidth = FVector2D::ZeroVector;
	mutable TOptional<double> DraftWidth;
	mutable FString SizingError;
	mutable bool bHasDimensions = false;
};
