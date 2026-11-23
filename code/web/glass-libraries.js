/** Catalog views and explicit library edits derived from per-surface policies.
 * The sidebar is a union, not a second global selector. Rendering it must not
 * rewrite archived defaults, enable search or broaden any surface's library.
 */
import {clone} from './model.js';

// Shared by all ten saved formal benchmarks. Existing per-surface flags,
// including task-specific strict_libraries, are never reset when choosing a library.
const newSurfaceDefaults={enabled:false,max_candidates:64};
export function libraryNames(value){
 const values=Array.isArray(value)?value:String(value??'').replaceAll(';',',').split(',');
 const seen=new Set();
 return values.map(name=>String(name??'').trim()).filter(name=>{
  const key=name.toUpperCase();if(!name||seen.has(key))return false;seen.add(key);return true;
 });
}

export function surfaceSearchLibraries(project,index){
 const system=project.system,search=system.glass_search??{};
 const cfg=search.surface_configs?.find(row=>Number(row.surface_index)===Number(index));
 for(const value of [cfg?.libraries,search.default_libraries,system.glass_libraries,system.glass_library]){
  const names=libraryNames(value);
  if(names.length)return names;
 }
 return ['SCHOTT']; // Same last-resort fallback as the native configuration contract.
}

export function materialLibrary(material){
 if(material.library)return String(material.library).trim();
 // A material reference can identify a measurement rather than a catalog
 // (e.g. SCHOTT/BK7/Lane.yml). Match the backend's file-based catalog identity.
 for(const source of [material.filename,material.source_path]){
  const match=String(source??'').replaceAll('\\','/').match(/(?:^|\/)glass\/([^/]+)/i);
  if(match)return match[1].toUpperCase();
 }
 return String(material.reference??'').trim();
}

export function glassLibraryEntries(project){
 const entries=new Map();
 const add=(name,index,role)=>{
  const key=name.toUpperCase();if(!name)return;
  if(!entries.has(key))entries.set(key,{name,searchSurfaces:[],materialSurfaces:[]});
  entries.get(key)[role].push(index);
 };
 for(const [index,surface] of project.system.surfaces.entries()){
  const material=surface.material_after??{};
  if(['object','image'].includes(surface.kind))continue;
  const configured=project.system.glass_search?.surface_configs?.some(row=>Number(row.surface_index)===index);
  const air=material.mode==='air'||(!material.mode&&(!material.name||material.name==='air'));
  if(air&&!configured)continue;
  // A configured air face is a planned library choice visible in the table.
  // Include it even before a real material is assigned, so edits update the union.
  for(const name of surfaceSearchLibraries(project,index))add(name,index,'searchSurfaces');
  if(material.mode==='glass')add(materialLibrary(material),index,'materialSurfaces');
 }
 return [...entries.values()];
}

export function systemGlassLibraries(project){
 return glassLibraryEntries(project).map(row=>row.name);
}

export function setSurfaceSearchLibraries(project,index,libraries){
 if(!Number.isInteger(index)||!project.system.surfaces[index]||['object','image'].includes(project.system.surfaces[index].kind))throw Error('Invalid material surface');
 const next=clone(project),search=next.system.glass_search??={};
 const configs=search.surface_configs??=[];
 let cfg=configs.find(row=>Number(row.surface_index)===index);
 if(!cfg){
  cfg={surface_index:index,...newSurfaceDefaults,enabled:search.enabled??newSurfaceDefaults.enabled,max_candidates:search.max_candidates_per_surface??newSurfaceDefaults.max_candidates};
  configs.push(cfg);
 }
 cfg.libraries=libraryNames(libraries);
 return next;
}

export function surfaceMaterialChoices(project,index,materials){
 const libraries=new Set(surfaceSearchLibraries(project,index).map(name=>name.toUpperCase()));
 return materials.map((record,index)=>({record,index})).filter(({record})=>
  libraries.has(materialLibrary(record.library?record:record.material??record).toUpperCase()));
}
