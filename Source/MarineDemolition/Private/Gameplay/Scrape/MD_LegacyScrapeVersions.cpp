#include "CoreMinimal.h"
#include "Serialization/CustomVersion.h"

namespace
{
// Retained designer levels/data use these package headers. Registration only;
// the retired cell reader, seed policy and wall contamination are not restored.
FCustomVersionRegistration LegacyCellVersion(
	FGuid(0x5AB38641, 0xAA834C67, 0xB7E165D9, 0x6CED20B3),
	1, TEXT("MDScrapeMeshUVCell"));
FCustomVersionRegistration LegacySeedVersion(
	FGuid(0xBC8E4F67, 0x6833487B, 0x9B6BA230, 0x9B7511AE),
	1, TEXT("MDContaminationSeedPolicy"));
}
