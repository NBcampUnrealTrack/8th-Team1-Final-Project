#include "Data/Drawing/MD_GlassDrawingSettings.h"

#include "Data/Drawing/MD_GlassDrawingProfile.h"

const UMD_GlassDrawingSettings* UMD_GlassDrawingSettings::Get()
{
	return GetDefault<UMD_GlassDrawingSettings>();
}

UMD_GlassDrawingProfile* UMD_GlassDrawingSettings::LoadDefaultProfile() const
{
	return DefaultProfile.LoadSynchronous();
}
