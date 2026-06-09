// Copyright (c) 2025 Nick McClure (Protospatial). All Rights Reserved.

#include "Core/N2CExportCommandlet.h"
#include "Core/N2CNodeCollector.h"
#include "Core/N2CNodeTranslator.h"
#include "Core/N2CSerializer.h"
#include "Models/N2CBlueprint.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"

UN2CExportCommandlet::UN2CExportCommandlet()
{
    IsClient = false;
    IsEditor = true;
    IsServer = false;
    LogToConsole = true;
}

int32 UN2CExportCommandlet::Main(const FString& Params)
{
    UE_LOG(LogTemp, Log, TEXT("[N2CExport] Starting Blueprint export commandlet"));

    FString AssetPath, PathPrefix, AssetListFile, OutputDir;
    FParse::Value(*Params, TEXT("asset="), AssetPath);
    FParse::Value(*Params, TEXT("path="), PathPrefix);
    FParse::Value(*Params, TEXT("assetlist="), AssetListFile);
    FParse::Value(*Params, TEXT("output="), OutputDir);
    const bool bPretty = FParse::Param(*Params, TEXT("pretty"));

    if (OutputDir.IsEmpty())
    {
        OutputDir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("NodeToCode/Export"));
    }
    FPaths::NormalizeDirectoryName(OutputDir);
    IFileManager::Get().MakeDirectory(*OutputDir, true);

    int32 ExportedCount = 0;
    if (!AssetPath.IsEmpty())
    {
        ExportedCount = ExportBlueprint(AssetPath, OutputDir, bPretty) ? 1 : 0;
        UE_LOG(LogTemp, Log, TEXT("[N2CExport] Single asset %s -> %s"),
            *AssetPath, ExportedCount > 0 ? TEXT("OK") : TEXT("FAILED"));
    }
    else if (!PathPrefix.IsEmpty())
    {
        ExportedCount = ExportByPath(PathPrefix, OutputDir, bPretty);
        UE_LOG(LogTemp, Log, TEXT("[N2CExport] Batch: %d blueprints exported from %s"),
            ExportedCount, *PathPrefix);
    }
    else if (!AssetListFile.IsEmpty())
    {
        ExportedCount = ExportFromList(AssetListFile, OutputDir, bPretty);
        UE_LOG(LogTemp, Log, TEXT("[N2CExport] List: %d blueprints exported"), ExportedCount);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("[N2CExport] No input specified. "
            "Use -asset=<path>, -path=<prefix>, or -assetlist=<file>"));
        return 1;
    }

    UE_LOG(LogTemp, Log, TEXT("[N2CExport] Done. %d blueprints exported to: %s"),
        ExportedCount, *OutputDir);
    return 0;
}

bool UN2CExportCommandlet::ExportBlueprint(const FString& PackagePath,
                                            const FString& OutputDir, bool bPretty)
{
    // Build full object path: /Game/BP/BP_Test -> /Game/BP/BP_Test.BP_Test
    const FString AssetName = FPackageName::GetLongPackageAssetName(PackagePath);
    const FString ObjectPath = PackagePath + TEXT(".") + AssetName;

    UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *ObjectPath);
    if (!BP)
    {
        UE_LOG(LogTemp, Warning, TEXT("[N2CExport] Failed to load Blueprint: %s"), *PackagePath);
        return false;
    }

    // Collect graphs: UbergraphPages first (auto-discovers referenced function graphs),
    // then remaining FunctionGraphs and MacroGraphs
    TArray<UEdGraph*> AllGraphs;
    AllGraphs.Append(BP->UbergraphPages);
    AllGraphs.Append(BP->FunctionGraphs);
    AllGraphs.Append(BP->MacroGraphs);

    if (AllGraphs.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[N2CExport] Blueprint has no graphs: %s"), *PackagePath);
        return false;
    }

    FN2CBlueprint MergedBlueprint;
    TSet<FString> ProcessedGraphNames;
    bool bHasMetadata = false;

    for (UEdGraph* Graph : AllGraphs)
    {
        if (!Graph) continue;

        const FString GraphName = Graph->GetName();
        if (ProcessedGraphNames.Contains(GraphName)) continue;

        TArray<UK2Node*> Nodes;
        FN2CNodeCollector::Get().CollectNodesFromGraph(Graph, Nodes);

        if (Nodes.IsEmpty())
        {
            ProcessedGraphNames.Add(GraphName);
            continue;
        }

        if (!FN2CNodeTranslator::Get().GenerateN2CStruct(Nodes)) continue;

        const FN2CBlueprint& Result = FN2CNodeTranslator::Get().GetN2CBlueprint();

        // Take version and metadata from the first successful run
        if (!bHasMetadata)
        {
            MergedBlueprint.Version  = Result.Version;
            MergedBlueprint.Metadata = Result.Metadata;
            bHasMetadata = true;
        }

        // Merge graphs, skipping any already processed (e.g. auto-discovered sub-graphs)
        for (const FN2CGraph& G : Result.Graphs)
        {
            if (!ProcessedGraphNames.Contains(G.Name))
            {
                MergedBlueprint.Graphs.Add(G);
                ProcessedGraphNames.Add(G.Name);
            }
        }

        // Merge structs with dedup by name
        for (const FN2CStruct& S : Result.Structs)
        {
            const bool bExists = MergedBlueprint.Structs.ContainsByPredicate(
                [&S](const FN2CStruct& E){ return E.Name == S.Name; });
            if (!bExists)
                MergedBlueprint.Structs.Add(S);
        }

        // Merge enums with dedup by name
        for (const FN2CEnum& En : Result.Enums)
        {
            const bool bExists = MergedBlueprint.Enums.ContainsByPredicate(
                [&En](const FN2CEnum& E){ return E.Name == En.Name; });
            if (!bExists)
                MergedBlueprint.Enums.Add(En);
        }
    }

    if (MergedBlueprint.Graphs.IsEmpty())
    {
        UE_LOG(LogTemp, Warning, TEXT("[N2CExport] No processable graphs in: %s"), *PackagePath);
        return false;
    }

    FN2CSerializer::SetPrettyPrint(bPretty);
    const FString JsonContent = FN2CSerializer::ToJson(MergedBlueprint);

    const FString FilePath = GetOutputFilePath(PackagePath, OutputDir);
    IFileManager::Get().MakeDirectory(*FPaths::GetPath(FilePath), true);

    if (!FFileHelper::SaveStringToFile(JsonContent, *FilePath,
            FFileHelper::EEncodingOptions::ForceUTF8))
    {
        UE_LOG(LogTemp, Error, TEXT("[N2CExport] Failed to write file: %s"), *FilePath);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("[N2CExport] Exported: %s -> %s"), *PackagePath, *FilePath);
    return true;
}

int32 UN2CExportCommandlet::ExportByPath(const FString& PathPrefix,
                                          const FString& OutputDir, bool bPretty)
{
    IAssetRegistry& AR =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry")).Get();

    UE_LOG(LogTemp, Log, TEXT("[N2CExport] Scanning assets under: %s"), *PathPrefix);
    AR.SearchAllAssets(true);  // Block until discovery complete

    FARFilter Filter;
    Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
    Filter.PackagePaths.Add(FName(*PathPrefix));
    Filter.bRecursivePaths = true;

    TArray<FAssetData> Assets;
    AR.GetAssets(Filter, Assets);

    UE_LOG(LogTemp, Log, TEXT("[N2CExport] Found %d Blueprint assets"), Assets.Num());

    int32 Count = 0;
    for (const FAssetData& AssetData : Assets)
    {
        if (ExportBlueprint(AssetData.PackageName.ToString(), OutputDir, bPretty))
            ++Count;
    }
    return Count;
}

int32 UN2CExportCommandlet::ExportFromList(const FString& ListFilePath,
                                            const FString& OutputDir, bool bPretty)
{
    TArray<FString> Lines;
    if (!FFileHelper::LoadFileToStringArray(Lines, *ListFilePath))
    {
        UE_LOG(LogTemp, Error, TEXT("[N2CExport] Cannot read asset list file: %s"), *ListFilePath);
        return 0;
    }

    int32 Count = 0;
    for (const FString& Line : Lines)
    {
        const FString AssetPath = Line.TrimStartAndEnd();
        if (AssetPath.IsEmpty() || AssetPath.StartsWith(TEXT("#"))) continue;

        if (ExportBlueprint(AssetPath, OutputDir, bPretty))
            ++Count;
    }
    return Count;
}

FString UN2CExportCommandlet::GetOutputFilePath(const FString& PackagePath,
                                                  const FString& OutputDir) const
{
    // Strip leading slash, then combine with output dir and append .json
    // /Game/Blueprints/BP_Test -> <OutputDir>/Game/Blueprints/BP_Test.json
    const FString RelPath = PackagePath.StartsWith(TEXT("/"))
        ? PackagePath.Mid(1)
        : PackagePath;
    return FPaths::Combine(OutputDir, RelPath + TEXT(".json"));
}
