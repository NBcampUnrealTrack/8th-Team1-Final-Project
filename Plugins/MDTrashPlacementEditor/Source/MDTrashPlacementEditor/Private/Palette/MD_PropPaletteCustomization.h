#pragma once

#include "IDetailCustomization.h"

/** Embeds the project palette only in Prop Placement's property panel. */
class FMD_PropPaletteCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();
	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
