// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "N2CExportCommandlet.generated.h"

/**
 * Commandlet for batch/incremental Blueprint-to-JSON export.
 *
 * Usage:
 *   UE5Editor.exe Project.uproject -run=N2CExport -asset=/Game/BP/BP_Test -output=D:/out
 *   UE5Editor.exe Project.uproject -run=N2CExport -path=/Game/ -output=D:/out
 *   UE5Editor.exe Project.uproject -run=N2CExport -assetlist=D:/list.txt -output=D:/out [-pretty]
 *
 * Output path convention: <OutputDir>/<PackagePath without leading slash>.json
 *   e.g. /Game/Blueprints/BP_Test -> <OutputDir>/Game/Blueprints/BP_Test.json
 */
UCLASS()
class UN2CExportCommandlet : public UCommandlet
{
    GENERATED_BODY()

public:
    UN2CExportCommandlet();
    virtual int32 Main(const FString& Params) override;

private:
    /** Export a single Blueprint by package path (e.g. /Game/Blueprints/BP_Test) */
    bool ExportBlueprint(const FString& PackagePath, const FString& OutputDir, bool bPretty);

    /** Batch export all Blueprints under PathPrefix using AssetRegistry scan */
    int32 ExportByPath(const FString& PathPrefix, const FString& OutputDir, bool bPretty);

    /** Incremental export: read asset paths from a text file (one per line) */
    int32 ExportFromList(const FString& ListFilePath, const FString& OutputDir, bool bPretty);

    /** Map UE package path -> output .json file path */
    FString GetOutputFilePath(const FString& PackagePath, const FString& OutputDir) const;
};
