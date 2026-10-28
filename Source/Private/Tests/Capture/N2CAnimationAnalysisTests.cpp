// Copyright (c) 2026. SPDX-License-Identifier: Apache-2.0
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Core/N2CAnimationCapture.h"
#include "Core/N2CCapture.h"
#include "Core/N2CLiveness.h"
#include "Core/N2CSerializer.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "Modules/ModuleManager.h"
#include "Tests/N2CTestBlueprintFactory.h"
#include "Tests/N2CTestHelpers.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CAnimationAnalysisTest, "NodeToCode.Capture.AnimationAnalysis", N2C_TEST_FLAGS)
bool FN2CAnimationAnalysisTest::RunTest(const FString& Parameters)
{
    // Load the engine editor module only in this test. Product code uses reflected class identities.
    FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("AnimGraph"));
    UClass* SchemaClass = FindObject<UClass>(nullptr, TEXT("/Script/AnimGraph.AnimationStateMachineSchema"));
    UClass* EntryClass = FindObject<UClass>(nullptr, TEXT("/Script/AnimGraph.AnimStateEntryNode"));
    UClass* StateClass = FindObject<UClass>(nullptr, TEXT("/Script/AnimGraph.AnimStateNode"));
    if (!TestNotNull(TEXT("state schema"), SchemaClass) || !TestNotNull(TEXT("entry class"), EntryClass)
        || !TestNotNull(TEXT("state class"), StateClass)) return false;

    N2CTest::FTestBlueprint BP(TEXT("BP_AnimationAnalysis"));
    UEdGraph* Graph = NewObject<UEdGraph>(BP.GetBlueprint(), TEXT("StateMachine"));
    Graph->Schema = SchemaClass;
    Graph->GraphGuid = FGuid::NewGuid();
    UEdGraphNode* Entry = NewObject<UEdGraphNode>(Graph, EntryClass);
    UEdGraphNode* State = NewObject<UEdGraphNode>(Graph, StateClass);
    for (UEdGraphNode* Node : {Entry, State})
    {
        Graph->Nodes.Add(Node);
        Node->CreateNewGuid();
    }
    UEdGraphPin* Output = Entry->CreatePin(EGPD_Output, TEXT("exec"), TEXT("Entry"));
    UEdGraphPin* Input = State->CreatePin(EGPD_Input, TEXT("Transition"), TEXT("In"));
    Input->bHidden = true; // Transition nodes use hidden pins; topology must not disappear with visibility.
    Output->MakeLinkTo(Input);
    FN2CCaptureOptions Options;
    Options.Depth = 0;
    auto Captured = N2C::CaptureGraph(*Graph, Options);
    if (!TestTrue(TEXT("non-K2 state graph is captured"), Captured.HasValue())) return false;
    const FN2CGraph& Result = Captured.GetValue().Blueprint.Graphs[0];
    TestEqual(TEXT("entry and state survive"), Result.Nodes.Num(), 2);
    TestEqual(TEXT("state edge survives"), Result.Flows.Data.Num(), 1);
    TestEqual(TEXT("state entry is not a K2 execution edge"), Result.Flows.Execution.Num(), 0);
    TestEqual(TEXT("entry category is retained"), Result.Nodes[0].OutputPins[0].SubType, FString(TEXT("StateLink:exec")));
    TestTrue(TEXT("animation liveness explicitly not analyzed"), Result.bAnalysisOnly);
    TestEqual(TEXT("source graph identity"), Result.SourcePath, Graph->GetPathName());
    TestFalse(TEXT("entry not falsely unused"), Result.Nodes[0].bUnused);

    FN2CBlueprint RoundTrip;
    TestTrue(TEXT("analysis JSON parses"), FN2CSerializer::FromJson(FN2CSerializer::ToJson(Captured.GetValue().Blueprint, EN2CJsonStyle::Condensed), RoundTrip));
    if (RoundTrip.Graphs.Num())
    {
        TestTrue(TEXT("analysis flag roundtrips"), RoundTrip.Graphs[0].bAnalysisOnly);
        TestEqual(TEXT("source path roundtrips"), RoundTrip.Graphs[0].SourcePath, Result.SourcePath);
    }

    // Ordinary K2 graphs keep their previous dead-code behavior.
    FN2CGraph K2;
    FN2CNodeDefinition& Dead = K2.Nodes.AddDefaulted_GetRef();
    Dead.ID = TEXT("N1");
    Dead.NodeType = EN2CNodeType::Unknown;
    N2CLiveness::MarkGraph(K2);
    TestTrue(TEXT("unused pure K2 node is still unused"), K2.Nodes[0].bUnused);
    TestFalse(TEXT("unrelated graph is not an animation graph"), N2CAnimationCapture::IsAnimationGraph(BP.EventGraph()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FN2CPropertyAccessCaptureTest, "NodeToCode.Capture.PropertyAccessPath", N2C_TEST_FLAGS)
bool FN2CPropertyAccessCaptureTest::RunTest(const FString& Parameters)
{
    // Optional plugin: the adapter must also work when the plugin is absent. Absence is reported, not a fake class.
    FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("PropertyAccessNode"));
    UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/PropertyAccessNode.K2Node_PropertyAccess"));
    if (!Class)
    {
        AddInfo(TEXT("PropertyAccessNode not loaded; actual path capture is covered by the Lyra live probe."));
        return true;
    }
    N2CTest::FTestBlueprint BP(TEXT("BP_PropertyAccessPath"));
    UK2Node* Node = NewObject<UK2Node>(BP.EventGraph(), Class);
    FArrayProperty* Path = FindFProperty<FArrayProperty>(Class, TEXT("Path"));
    FStrProperty* Segment = Path ? CastField<FStrProperty>(Path->Inner) : nullptr;
    if (!TestNotNull(TEXT("Path array exists"), Path) || !TestNotNull(TEXT("Path segments are strings"), Segment)) return false;
    FScriptArrayHelper Values(Path, Path->ContainerPtrToValuePtr<void>(Node));
    Values.AddValues(2);
    Segment->SetPropertyValue(Values.GetRawPtr(0), TEXT("TryGetPawnOwner"));
    Segment->SetPropertyValue(Values.GetRawPtr(1), TEXT("GetVelocity"));
    FN2CNodeDefinition Described;
    N2CAnimationCapture::ReadNode(*Node, Described);
    TestEqual(TEXT("ordered segments"), Described.PropertyAccessPath, TArray<FString>{TEXT("TryGetPawnOwner"), TEXT("GetVelocity")});
    // The reader did not resolve, execute, or rewrite the path.
    TestEqual(TEXT("source unchanged"), Segment->GetPropertyValue(Values.GetRawPtr(1)), FString(TEXT("GetVelocity")));
    return true;
}
#endif
