// Read-only regression probe. Uses AITA's existing native MCP client as an external test dependency.
// node lyra_animation_readback.mjs <native-client.mjs> <baseline directory> <output directory>
import fs from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import assert from 'node:assert/strict';
const [clientPath,baselineRoot,outputRoot]=process.argv.slice(2);
const {createNativeClient}=await import(pathToFileURL(path.resolve(clientPath)));
fs.mkdirSync(outputRoot,{recursive:true});
const c=createNativeClient({port:27000,timeout:120000});
const samples=JSON.parse(fs.readFileSync(path.join(baselineRoot,'summary.json'),'utf8'));
const results=[];
async function read(args){
 const raw=await c.call('get-blueprint-n2cjson',args);
 assert.ok(!raw.isError,JSON.stringify(raw));
 let doc=JSON.parse(raw.content[0].text);
 if(doc.path)doc=JSON.parse(fs.readFileSync(doc.path,'utf8').replace(/^\uFEFF/,''));
 return doc;
}
const median=xs=>[...xs].sort((a,b)=>a-b)[Math.floor(xs.length/2)];
for(const sample of samples){
 const expected=JSON.parse(fs.readFileSync(path.join(baselineRoot,'n2c'+sample.asset+'.json.baseline.json'),'utf8'));
 const before=JSON.parse(fs.readFileSync(path.join(baselineRoot,'dev2-final.json'),'utf8')).find(x=>x.asset===sample.asset);
 let doc;const timings=[];
 for(let i=0;i<6;i++){
  const start=performance.now();doc=await read({blueprintPath:sample.asset,depth:0,compact:true});timings.push(performance.now()-start);
 }
 let expectedNodes=0,expectedGraphs=0,pathChecks=0,settingsChecks=0,stateEdges=0;
 for(const graph of expected.graphs){
  const nodes=graph.nodes.filter(n=>(n.k2&&!n.class.endsWith('.K2Node_Knot'))||/\/AnimGraph\.AnimState(?:Node|TransitionNode|AliasNode|ConduitNode|EntryNode)$/.test(n.class));
  if(!nodes.length)continue;
  expectedNodes+=nodes.length;expectedGraphs++;
  const actual=doc.graphs.find(g=>g.source_path===graph.path)||doc.graphs.find(g=>!g.source_path&&g.name===graph.path.split(':').at(-1));
  assert.ok(actual,'Missing graph '+graph.path);
  assert.equal(actual.nodes.length,nodes.length,'Node count '+graph.path);
  if(actual.analysis_only){assert.equal(actual.nodes.filter(n=>n.unused||n.unreachable).length,0);assert.ok(actual.schema_class);}
  for(const node of nodes.filter(n=>n.class.endsWith('.K2Node_PropertyAccess'))){
   const segments=Array.from(node.Path.matchAll(/"([^"]*)"/g),m=>m[1]);
   assert.ok(actual.nodes.some(n=>JSON.stringify(n.property_access_path)===JSON.stringify(segments)),'PropertyAccess '+node.Path);pathChecks++;
  }
  for(const node of nodes.filter(n=>n.Node)){
   assert.ok(actual.nodes.some(n=>n.class===node.class.split('.').at(-1)&&n.editor_properties?.Node===node.Node),'Node settings '+node.path);settingsChecks++;
  }
  if(graph.class.endsWith('.AnimationStateMachineGraph')){
   const edges=nodes.flatMap(n=>n.pins.filter(p=>!p.input).flatMap(p=>p.links));
   const actualEdges=Object.values(actual.flows.data||{}).reduce((n,v)=>n+(Array.isArray(v)?v.length:1),0)+(actual.flows.execution?.length||0);
   assert.equal(actualEdges,edges.length,'State edges '+graph.path);stateEdges+=edges.length;
   const identity=n=>n.class.split('.').at(-1)+'|'+(n.BoundGraph||n.editor_properties?.BoundGraph||n.title||'');
   const sourceNames=new Map(nodes.map(n=>[n.path,identity(n)]));
   const expectedEdges=nodes.flatMap(n=>n.pins.filter(p=>!p.input).flatMap(p=>p.links.map(link=>{
    const split=link.lastIndexOf(':');return identity(n)+'['+p.name+']->'+sourceNames.get(link.slice(0,split))+'['+link.slice(split+1)+']';
   }))).sort();
   const pins=new Map(actual.nodes.flatMap(n=>[...n.input_pins,...n.output_pins].map(p=>[n.ids.short+'.'+p.ids.short,identity(n)+'['+p.name+']'])));
   const capturedEdges=Object.entries(actual.flows.data||{}).flatMap(([from,to])=>(Array.isArray(to)?to:[to]).map(target=>pins.get(from)+'->'+pins.get(target))).sort();
   assert.deepEqual(capturedEdges,expectedEdges,'State edge endpoints '+graph.path);
  }
 }
 assert.equal(doc.graphs.length,expectedGraphs,'Graph count '+sample.asset);
 assert.equal(doc.graphs.flatMap(g=>g.nodes).length,expectedNodes,'Total nodes '+sample.asset);
 if(!sample.name.startsWith('ABP_')){
  // UE reconstructs some legacy pins on load (e.g. TransformScaleMethod). Compare semantics and node/graph GUIDs,
  // while allowing only pin GUIDs to differ across editor processes. Short IDs, names, types and edges must match.
  const withoutPinGuids=source=>{
   const copy=structuredClone(source);
   for(const g of copy.graphs)for(const n of g.nodes)for(const p of [...n.input_pins,...n.output_pins])delete p.ids.guid;
   for(const p of Object.values(copy.guids?.pins||{}))delete p.guid;
   return copy;
  };
  assert.deepEqual(withoutPinGuids(doc),withoutPinGuids(before.doc),'Ordinary K2 output unchanged '+sample.asset);
 }
 const result={asset:sample.asset,graphs:doc.graphs.length,nodes:expectedNodes,pathChecks,settingsChecks,stateEdges,medianMs:median(timings.slice(1)),timings,bytes:Buffer.byteLength(JSON.stringify(doc)),beforeGraphs:before.doc.graphs.length,beforeNodes:before.doc.graphs.flatMap(g=>g.nodes).length};
 results.push(result);fs.writeFileSync(path.join(outputRoot,sample.name+'.json'),JSON.stringify(doc,null,2));
 fs.writeFileSync(path.join(outputRoot,'results.json'),JSON.stringify(results,null,2));console.log(JSON.stringify(result));
}
// This used to fail with EMPTY_GRAPH even though list-blueprint-graphs reported 47 nodes.
const state=await read({blueprintPath:samples[2].asset,graphName:'LocomotionSM',depth:0});
assert.equal(state.graphs[0].nodes.length,45); // two comment boxes are represented separately
assert.equal(state.graphs[0].comments.length,2);
console.log('PASS: all five assets and standalone state-machine readback; source assets were only read.');
