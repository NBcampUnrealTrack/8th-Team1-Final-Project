// Fill out your copyright notice in the Description page of Project Settings.


#include "Online/MD_RoomCode.h"
#include "Misc/SecureHash.h"

const FMD_RoomCode FMD_RoomCode::EmptyRoomCode;

FMD_RoomCode::FMD_RoomCode()
{
}

FMD_RoomCode FMD_RoomCode::Random()
{
	// Room Code를 생성할 기본 고유값으로 GUID 사용
	FGuid Guid = FGuid::NewGuid();
	
	// GUID를 해시 데이터로 변환
	uint8 Hash[FSHA1::DigestSize];
	FSHA1::HashBuffer(&Guid, sizeof(Guid), Hash);

	// Room Code에 사용될 수 있는 문자 집합 얻기
	const FString& CharacterCandidates = GetCharacterCandidates();

	FMD_RoomCode RoomCode;

	// Room Code 작성
	int32 SectorCount = sizeof(CodeLengths) / sizeof(uint8);
	int32 CharacterCounter = 0;
	for (int32 i = 0; i < SectorCount; ++i)
	{
		uint8 SectorLength = CodeLengths[i];
		FString& SectorCode = RoomCode.RoomCodes.AddDefaulted_GetRef();

		for (uint8 j = 0; j < SectorLength; ++j)
		{
			int32 BitOffset = CharacterCounter * 5;
			int32 ByteIndex = BitOffset / 8;
			int32 BitIndex = BitOffset % 8;

			uint16 Window = static_cast<uint16>(Hash[ByteIndex]) | (static_cast<uint16>(Hash[ByteIndex + 1]) << 8);

			uint8 CharacterIndex = static_cast<uint8>((Window >> BitIndex) & 0x1F);

			SectorCode.AppendChar(CharacterCandidates[CharacterIndex]);
			++CharacterCounter;
		}
	}

	return RoomCode;
}

FMD_RoomCode FMD_RoomCode::Parse(FString StringRoomCode)
{
	// 공백 및 Hypen 문자 제거
	StringRoomCode.ReplaceInline(TEXT(" "), TEXT(""));
	StringRoomCode.ReplaceInline(TEXT("-"), TEXT(""));

	FMD_RoomCode RoomCode;

	// Room Code 작성
	int32 Left = 0;
	int32 SectorCount = sizeof(CodeLengths) / sizeof(uint8);
	for (int32 i = 0; i < SectorCount; ++i)
	{
		uint8 SectorLength = CodeLengths[i];

		if (Left + SectorLength > StringRoomCode.Len())
		{
			return EmptyRoomCode;
		}

		FString& SectorCode = RoomCode.RoomCodes.AddDefaulted_GetRef();
		SectorCode = StringRoomCode.Mid(Left, SectorLength);

		SectorCode = SectorCode.ToUpper();

		Left += SectorLength;
	}

	if (RoomCode.IsValid() == false)
	{
		return EmptyRoomCode;
	}

	return RoomCode;
}

FString FMD_RoomCode::ToString() const
{
	return IsValid() ? FString::Join(RoomCodes, TEXT("-")) : FString();
}

bool FMD_RoomCode::IsValid() const
{
	int32 SectorCount = sizeof(CodeLengths) / sizeof(uint8);
	if (RoomCodes.Num() != SectorCount)
	{
		return false;
	}

	const FString& CharacterCandidates = GetCharacterCandidates();
	for (int32 i = 0; i < SectorCount; ++i)
	{
		const FString& SectorCode = RoomCodes[i];
		if (SectorCode.Len() != CodeLengths[i])
		{
			return false;
		}

		for (const TCHAR& CodeCharacter : SectorCode)
		{
			int32 CandidateIndex = INDEX_NONE;
			if (CharacterCandidates.FindChar(CodeCharacter, CandidateIndex) == false)
			{
				return false;
			}
		}
	}

	return true;
}

const FString& FMD_RoomCode::GetCharacterCandidates()
{
	static const FString CharacterCandidates = TEXT("ABCDEFGHJKLMNPQRSTUVWXYZ23456789");
	return CharacterCandidates;
}
