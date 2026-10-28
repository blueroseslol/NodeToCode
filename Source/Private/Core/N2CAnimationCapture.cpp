// Copyright (c) 2026. SPDX-License-Identifier: Apache-2.0
#include "Core/N2CAnimationCapture.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphSchema.h"
#include "K2Node.h"
#include "Models/N2CNode.h"
#include "UObject/UnrealType.h"

namespace
{
    bool DerivesFrom(const UObject* Object, const TCHAR* ClassPath)
    {
        for (const UClass* Class = Object ? Object->GetClass() : nullptr; Class; Class = Class->GetSuperClass())
        {
            if (Class->GetPathName() == ClassPath) return true;
        }
        return false;
    }
}

bool N2CAnimationCapture::IsAnimationGraph(const UEdGraph* Graph)
{
    return Graph && (DerivesFrom(Graph->GetSchema(), TEXT("/Script/AnimGraph.AnimationGraphSchema"))
        || DerivesFrom(Graph->GetSchema(), TEXT("/Script/AnimGraph.AnimationStateMachineSchema"))
        || DerivesFrom(Graph->GetSchema(), TEXT("/Script/AnimGraph.AnimationTransitionSchema"))
        || DerivesFrom(Graph->GetSchema(), TEXT("/Script/AnimGraph.AnimationConduitGraphSchema")));
}

bool N2CAnimationCapture::IsStateMachineGraph(const UEdGraph* Graph)
{
    return Graph && DerivesFrom(Graph->GetSchema(), TEXT("/Script/AnimGraph.AnimationStateMachineSchema"));
}

bool N2CAnimationCapture::CanCapture(const UEdGraphNode* Node)
{
    // Non-K2 support is intentionally limited to the animation state-machine node family. RigVM is not K2.
    return Node && (Node->IsA<UK2Node>() ||
        (IsStateMachineGraph(Node->GetGraph()) && (DerivesFrom(Node, TEXT("/Script/AnimGraph.AnimStateNodeBase"))
            || DerivesFrom(Node, TEXT("/Script/AnimGraph.AnimStateEntryNode")))));
}

void N2CAnimationCapture::ReadNode(const UEdGraphNode& Node, FN2CNodeDefinition& OutNode)
{
    if (DerivesFrom(&Node, TEXT("/Script/PropertyAccessNode.K2Node_PropertyAccess")))
    {
        const FArrayProperty* Path = FindFProperty<FArrayProperty>(Node.GetClass(), TEXT("Path"));
        const FStrProperty* Segment = Path ? CastField<FStrProperty>(Path->Inner) : nullptr;
        if (Path && Segment)
        {
            FScriptArrayHelper Values(Path, Path->ContainerPtrToValuePtr<void>(&Node));
            for (int32 Index = 0; Index < Values.Num(); ++Index)
                OutNode.PropertyAccessPath.Add(Segment->GetPropertyValue(Values.GetRawPtr(Index)));
        }
    }
    if (!IsAnimationGraph(Node.GetGraph())) return;

    // Include the declaring class's reflected properties only: avoid copying UEdGraphNode editor layout/state.
    // Scalar/struct/object-reference exports do not execute functions or recursively traverse objects.
    for (TFieldIterator<FProperty> It(Node.GetClass()); It; ++It)
    {
        const UClass* Owner = Cast<UClass>(It->GetOwnerStruct());
        if (!Owner || Owner == UEdGraphNode::StaticClass() || Owner == UK2Node::StaticClass()) continue;
        FString Value;
        It->ExportText_InContainer(0, Value, &Node, nullptr, const_cast<UEdGraphNode*>(&Node), PPF_None);
        if (!Value.IsEmpty()) OutNode.EditorProperties.Add(It->GetName(), MoveTemp(Value));
    }
}
