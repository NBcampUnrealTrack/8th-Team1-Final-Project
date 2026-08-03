// Fill out your copyright notice in the Description page of Project Settings.


#include "Data/Save/MD_SaveData.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Util/MD_LogUtility.h"

bool FMD_SaveData::SaveDataToJson(const FString& FileName, const FMD_SaveData& Data)
{
    // 변환된 Json 문자열을 저장하는 변수
    FString JsonString;

    // 구조체를 Json 문자열으로 변환
    bool bWasConvertSuccess = FJsonObjectConverter::UStructToJsonObjectString(Data, JsonString);

    // Json 변환 실패 시 함수 조기 종료
    if (bWasConvertSuccess == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Json 문자열 변환에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
        return false;
    }

    // Project의 Saved 폴더 내부에 위치한 JsonData 폴더 경로 설정 (Json 파일 저장 위치)
    FString DirectoryPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("JsonData"));

    // 플랫폼의 파일 시스템 관리 객체 얻기
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

    // Json 파일을 저장하려는 폴더의 경로 존재 확인
    if (PlatformFile.DirectoryExists(*DirectoryPath) == false)
    {
        // 폴더 생성
        bool bWasDirectoryCreated = PlatformFile.CreateDirectoryTree(*DirectoryPath);

        // 폴더 생성 실패 시 함수 조기 종료
        if (bWasDirectoryCreated == false)
        {
            // 로그 출력
            UE_LOG(MarineDemolition, Error, TEXT("%s 데이터를 저장하려는 폴더 생성에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
            return false;
        }
    }

    // 파일 경로 설정
    FString FilePath = FPaths::Combine(DirectoryPath, FileName);

    // UTF-8 형식으로 Json 파일 저장
    bool bWasFileSaved = FFileHelper::SaveStringToFile(JsonString, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

    // 파일 저장 성공 여부 검사
    if (bWasFileSaved == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Json 파일 저장에 실패하였습니다."), *UE_CALL_LOCATIONINFO);
        return false;
    }

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s 데이터를 Json 파일으로 저장하였습니다. (FilePath : %s)"), *UE_CALL_LOCATIONINFO, *FilePath);
    return true;
}

bool FMD_SaveData::LoadDataFromJson(const FString& FileName, FMD_SaveData& Data)
{
    // Project의 Saved 폴더 내부에 위치한 JsonData 폴더 경로 설정 (Json 파일 저장 위치)
    FString DirectoryPath = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("JsonData"));

    // 파일 경로 설정
    FString FilePath = FPaths::Combine(DirectoryPath, FileName);

    // 읽어들인 Json 문자열을 저장하는 변수
    FString JsonString;

    // 파일의 Json 문자열 읽기
    bool bWasFileLoaded = FFileHelper::LoadFileToString(JsonString, *FilePath);

    // 파일 읽기 성공 여부 검사
    if (bWasFileLoaded == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Error, TEXT("%s Json 파일을 읽어오지 못하였습니다."), *UE_CALL_LOCATIONINFO);
        return false;
    }

    // Json 문자열을 USTRUCT로 변환에 실패하는 경우 원인에 대한 문자열을 저장하는 변수
    FText FailReason;

    // Json 문자열을 USTRUCT로 변환
    bool bWasConvertSuccess = FJsonObjectConverter::JsonObjectStringToUStruct(JsonString, &Data, 0, 0, false, &FailReason);

    // 변환 성공 여부 검사
    if (bWasConvertSuccess == false)
    {
        // 로그 출력
        UE_LOG(MarineDemolition, Warning, TEXT("%s Json 문자열을 USTRUCT 객체로 변환에 실패하였습니다. (%s)"), *UE_CALL_LOCATIONINFO, *FailReason.ToString());
        return false;
    }

    // 로그 출력
    UE_LOG(MarineDemolition, Log, TEXT("%s Json 파일으로부터 데이터를 불러왔습니다. (FilePath : %s)"), *UE_CALL_LOCATIONINFO, *FilePath);
    return true;
}
