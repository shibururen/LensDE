export const clone = value => structuredClone(value);
export function getAt(value, path) {
  return path.reduce((current, key) => current?.[key], value);
}
export function setAt(project, path, value) {
  if (!path.length) throw new Error("An explicit scientific field is required");
  const next = clone(project);
  let parent = next;
  for (const key of path.slice(0, -1)) {
    if (parent?.[key] === undefined) throw new Error(`Missing saved field: ${path.join(".")}`);
    parent = parent[key];
  }
  if (parent === null || typeof parent !== "object") throw new Error("Invalid scientific field parent");
  parent[path.at(-1)] = value;
  return next;
}
export function assertProject(value) {
  if (!value || typeof value !== "object" || Array.isArray(value)) throw new Error("invalidProject");
  for (const key of ["system", "variables", "merit", "options"]) {
    if (!value[key] || typeof value[key] !== "object") throw new Error("invalidProject");
  }
  if (!Array.isArray(value.system.surfaces)) throw new Error("invalidProject");
  if (!Array.isArray(value.variables) && !Array.isArray(value.variables.variables)) throw new Error("invalidProject");
  return value;
}
export function parseNumeric(raw, nullable = false) {
  if (raw.trim() === "") {
    if (nullable) return null;
    throw new Error("invalidNumber");
  }
  const value = Number(raw);
  if (!Number.isFinite(value)) throw new Error("invalidNumber");
  return value;
}
export const variablePath = project => Array.isArray(project?.variables) ? ["variables"] : ["variables", "variables"];
export const variableRows = project => getAt(project, variablePath(project)) ?? [];
export function scientificSnapshot(project) {
  // Locale, output directory and job controls never enter this function.
  assertProject(project);
  return clone(project);
}
export function jobPayload(project, kind, preset = null) {
  if (!["workflow", "trace", "optimize", "vignetting"].includes(kind)) throw new Error("Unsupported job kind");
  const payload = {project: scientificSnapshot(project), kind};
  if (preset?.id) {
    payload.recipe_id = preset.id;
    payload.recipe_arm = preset.arm;
    payload.recipe_coordinates = clone(preset.coordinates ?? {});
  }
  return payload;
}
export function basicOptimizationPayload(project,mode) {
  if(!['de','local'].includes(mode))throw new Error('Only DE or DLS is available');
  const payload=jobPayload(project,'optimize');
  payload.project.options.optimizer_mode=mode;
  payload.project.options.polish=false;
  return payload;
}
export function optimizationSelection(project){
  const mode=String(project?.options?.optimizer_mode??'de').toLowerCase().replace(/[\s-]+/g,'_');
  return {scope:['initial_structure_search','first_order_seeded','initial_search'].includes(mode)?'initial':'ordinary',
    ordinaryMode:['local','dls','damped_least_squares'].includes(mode)?'local':'de'};
}
export function selectOptimization(project,{scope,ordinaryMode='de'}){
  if(!['ordinary','initial'].includes(scope)||!['de','local'].includes(ordinaryMode))throw new Error('Unsupported optimization selection');
  // Persist only the selected entry point; both sets of algorithm settings stay intact.
  return setAt(project,['options','optimizer_mode'],scope==='initial'?'initial_structure_search':ordinaryMode);
}
export function optimizationPayload(project,{scope,ordinaryMode='de'}){
  // Dispatch from an owned copy so request overrides cannot change the saved project.
  if(scope==='ordinary')return basicOptimizationPayload(project,ordinaryMode);
  if(scope!=='initial')throw new Error('Unsupported optimization scope');
  const payload=jobPayload(project,'workflow');
  payload.project.options.optimizer_mode='initial_structure_search';
  return payload;
}
export function shouldApplyCompletedResult(job,currentJobId,startedRevision,currentRevision,appliedJobId) {
  return job?.status==='completed'&&Boolean(job.result?.project)&&job.job_id===currentJobId&&
    Number.isInteger(startedRevision)&&startedRevision===currentRevision&&appliedJobId!==currentJobId;
}
export function normalizeJob(response) {
  return response?.job ?? response;
}
export function isActiveJob(job) {
  return ["queued", "preparing", "running", "stopping"].includes(job?.status);
}
export function safeLocalUrl(raw, origin) {
  if (typeof raw !== "string" || !raw) return null;
  try {
    const url = new URL(raw, origin);
    return ["http:", "https:"].includes(url.protocol) && url.origin === new URL(origin).origin ? url.href : null;
  } catch { return null; }
}
export function escapeHtml(value) {
  return String(value ?? "").replace(/[&<>"']/g, character => ({"&":"&amp;", "<":"&lt;", ">":"&gt;", '"':"&quot;", "'":"&#39;"}[character]));
}
export function coordinateFields(record, arm, coordinates = {}) {
  const declared = record?.coordinate_schema ?? record?.coordinates_schema;
  const schema = Array.isArray(declared) ? declared : declared?.properties
    ? Object.entries(declared.properties).map(([key,value])=>({key,...value,required:declared.required?.includes(key)})) : [];
  if (!Array.isArray(record?.available_variants)) return schema;
  const variants = record.available_variants.filter(row=>row.arm===arm);
  if (!variants.length) return [];
  const savedKeys = [...new Set(variants.flatMap(row=>Object.keys(row.coordinates ?? {})))];
  const keys = [...new Set([...schema.map(row=>row.key??row.name).filter(key=>savedKeys.includes(key)),...savedKeys])];
  return keys.map((key,index)=>{
    const metadata = schema.find(row=>(row.key??row.name)===key) ?? {key};
    const matching = variants.filter(row=>keys.slice(0,index).every(previous=>coordinates[previous]===undefined || JSON.stringify(row.coordinates?.[previous])===JSON.stringify(coordinates[previous])));
    const values = [...new Map(matching.filter(row=>Object.hasOwn(row.coordinates??{},key)).map(row=>[JSON.stringify(row.coordinates[key]),row.coordinates[key]])).values()];
    const originalChoices=metadata.choices??metadata.enum??[];
    const choices=values.map(value=>{
      const choice=originalChoices.find(item=>JSON.stringify(typeof item==="object"?item.value:item)===JSON.stringify(value));
      return typeof choice==="object"&&choice!==null ? {...choice,value} : {value,label:String(value)};
    });
    return {...metadata,key,choices,required:variants.some(row=>Object.hasOwn(row.coordinates??{},key))};
  });
}
export function catalogMaterial(record) {
  const material=record?.material??record;
  if(!material||typeof material.mode!=="string"||!material.mode)throw new Error("materialModeMissing");
  if(record.material)return clone(record.material);
  const scientificKeys=["name","mode","reference","nd","vd","filename","source_path"];
  return Object.fromEntries(scientificKeys.filter(key=>Object.hasOwn(material,key)).map(key=>[key,clone(material[key])]));
}

export function syncExistingVariableInitials(project) {
  const next=clone(project);
  for(const variable of variableRows(next)) {
    const surface=next.system.surfaces[variable.index],field=next.system.fields[variable.index];
    let value;
    if(variable.kind==='curvature'&&surface)value=surface.radius==null||surface.radius===0?0:1/surface.radius;
    else if(['radius','thickness','conic'].includes(variable.kind)&&surface)value=surface[variable.kind];
    else if(variable.kind==='asphere_coeff'&&surface)value=surface.asphere_coefficients?.[variable.term??0];
    else if(variable.kind==='material_nd'&&surface)value=surface.material_after?.nd;
    else if(variable.kind==='material_vd'&&surface)value=surface.material_after?.vd;
    else if(['fvcx','fvcy','fvdx','fvdy'].includes(variable.kind)&&field)value=field[variable.kind];
    else if(variable.kind==='vignette_x'&&field)value=field.fvdx??field.vignette_x;
    else if(['vignette_y','vignette_magnitude_y','vignetting_magnitude_y'].includes(variable.kind)&&field)value=field.fvdy??field.vignette_y;
    else if(['vignette_center_y','vignette_shift_y'].includes(variable.kind)&&field)value=field.fvcy;
    else if(['vignette_asymmetry_y','vignetting_asymmetry_y'].includes(variable.kind)&&field) {
      const magnitude=field.fvdy??field.vignette_y;
      if(Number.isFinite(magnitude)&&Number.isFinite(field.fvcy))value=magnitude>1e-12?Math.max(-1,Math.min(1,field.fvcy/magnitude)):0;
    }
    if(Number.isFinite(value))variable.initial=value;
  }
  return next;
}
