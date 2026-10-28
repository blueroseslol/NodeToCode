# UE 5.8 动画蓝图读取修复与复测

2026-10-28；基线为上游 `dev-2.0` 的 `5e205239423eb51a78bddfb058ac0e1f70ca4309`。
本地仓库 `D:/MatrixTA/NodeToCode`，origin 指向 `blueroseslol/NodeToCode`，upstream 指向 `protospatial/NodeToCode`。
修复分支：`codex/ue58-blueprint-analysis`；另保留跟踪上游的本地 `dev-2.0` 分支。

## 修复范围

- 非 K2 动画状态机节点现在进入采集：入口、状态、转换、别名、conduit。注释仍单独保存。
- 状态机转换的隐藏引脚保留。Entry 的 `exec` 引脚不再错误套用 K2 执行流校验；状态拓扑保存在 `flows.data`，原始引脚分类保存在 `StateLink:<category>` 子类型中。
- 动画姿态、状态、转换和 conduit 图标记 `analysis_only`，保存完整 `source_path` 和 `schema_class`。不再因缺少 K2 执行入口而误标 `unused` / `unreachable`；这意味着没有推断活跃性，不是证明所有节点都会执行。
- Property Access 输出有序 `property_access_path`，不执行路径内的函数。
- `editor_properties` 保存动画节点的反射配置，包括内部 Node、绑定图、转换/别名配置、回调等；序列化/反序列化保留新增字段。
- 适配层仅使用 Engine/CoreUObject/BlueprintGraph 的基础 API，按完整类路径识别可选类型，不静态链接 PropertyAccessNode、Lyra、GAS 或 AnimGraph 类型。普通 K2 路径保持原有语义。

## Lyra 实测

项目：`D:/MatrixTA/LyraRPGGameplayAbility`。只读五个相同资产，使用上轮直接从引擎对象读取的快照核对。

| 资产 | 修复前 图/节点 | 修复后 图/节点 | 补证检查 |
|---|---:|---:|---|
| B_Hero_Default | 6 / 61 | 6 / 61 | 普通 K2 输出语义保持一致 |
| B_Hero_ShooterMannequin | 10 / 155 | 10 / 155 | 普通 K2 输出语义保持一致 |
| ABP_Mannequin_Base | 78 / 529 | 79 / 574 | 55 条状态机边、13 条属性路径、94 份 Node 配置 |
| ABP_Mannequin_CopyPose | 1 / 2 | 1 / 2 | 2 份 Node 配置；包含 bUseAttachedParent=True |
| ABP_ItemAnimLayersBase | 79 / 643 | 83 / 676 | 40 条状态机边、86 条属性路径、116 份 Node 配置 |

主状态机单独读取也通过：45 个语义节点、2 个注释框。全部 95 条状态机边逐项比较来源/目标节点及引脚，全部 99 条 Property Access 路径与 212 份 Node 配置比对通过。这里的节点数去除了转接节点，注释另计；不代表全部 UE 动画系统或运行时行为已经完整验证。

普通 K2 对照忽略 UE 加载旧资产时重建的 pin GUID；节点/图 GUID、短 ID、引脚名称/类型、默认值、连线与其他字段仍严格比较。没有修改或保存角色和动画资产。

## 性能复测

在同一 UE 5.8 编辑器中交替调用原生 MCP 批量节点详情和修复版 NodeToCode，共 6 次，剔除首轮取后 5 次中位数。N2C 含落盘结果的本地读取及解析；原生含完整 JSON 回传。没有并发编译或 Commandlet；没有调用 LLM。

| 资产 | 本轮原生 MCP | 本轮修复版 N2C | N2C 输出字节 |
|---|---:|---:|---:|
| B_Hero_Default | 438.0 ms | 12.1 ms | 69,809 |
| B_Hero_ShooterMannequin | 598.2 ms | 22.1 ms | 163,619 |
| ABP_Mannequin_Base | 4297.1 ms | 136.7 ms | 721,275 |
| ABP_Mannequin_CopyPose | 153.6 ms | 4.7 ms | 5,385 |
| ABP_ItemAnimLayersBase | 4380.7 ms | 135.6 ms | 1,101,008 |

本轮原生 MCP 耗时高于前一轮，存在会话/编辑器调度波动，不将跨轮比值解释为算法变化。新增状态机及配置使主动画输出从 462,619 增至 721,275 字节，动画层从 595,364 增至 1,101,008 字节。更大的输出换来了可验证的结构与配置；计时不包含 LLM 推理，也不说明语义分析准确率。

## 验证与边界

- 新增自动化测试覆盖非 K2 状态节点、exec→Transition 类型差异、隐藏引脚、序列化往返、Property Access 保序以及普通 K2 死代码判断。
- 最终代码的 NodeToCode.Capture 回归与新测试在 UE 5.6、5.7、5.8 上各通过 158 项（各含 29 项带警告的成功用例），0 失败、0 未执行；最终结果见本目录 evidence 下的自动化摘要。
- Lyra 真实资产及性能只在 UE 5.8 测试。CPython pytest 因本机 PATH 无 uv 被 build.ps1 跳过；没有宣称通过全量 `-All -Smoke -Test` 发布门禁。
- 未修改 Epic 原生 MCP 的 DSL 实现，也未修复其动画宏/姿态输出为空的问题。
- CDO、组件默认值、GameplayTag 映射仍需配合对象读取；原生 C++ 实现与运行时实例值仍需另外检查。UI 焦点工具及动画代码生成不在本次验收范围。

## 证据与复现

- `Tests/mcp_probe/lyra_animation_readback.mjs`：真实资产只读验证入口；所需外部基线见其 README。
- `Context/translation-pipeline/evidence/ue58-animation-readback/`：跟踪保存的结果与计时。
- `BuildLogs/lyra-fixed/*.json`：本机完整捕获；`BuildLogs/lyra-readback-final.txt`：逐项检查结果。
- `BuildLogs/comparison-fixed/comparison-timing.json`：最终每轮交替计时。
- `BuildLogs/Tests_<version>_<timestamp>/index.json`：每版自动化报告。
- 构建宿主位于 `D:/MatrixTA/NodeToCodeHosts/NodeToCodeHost_5_8`；其插件 Source 等目录链接到本仓库，Binaries/Intermediate 独立。
