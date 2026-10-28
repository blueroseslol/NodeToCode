// Copyright (c) 2026. SPDX-License-Identifier: Apache-2.0
#pragma once
#include "CoreMinimal.h"
class UEdGraph;
class UEdGraphNode;
struct FN2CNodeDefinition;

/** Reflection-only adapter: no static dependency on AnimGraph, PropertyAccessNode or a project's types. */
namespace N2CAnimationCapture
{
    bool IsAnimationGraph(const UEdGraph* Graph);
    bool IsStateMachineGraph(const UEdGraph* Graph);
    bool CanCapture(const UEdGraphNode* Node);
    void ReadNode(const UEdGraphNode& Node, FN2CNodeDefinition& OutNode);
}
