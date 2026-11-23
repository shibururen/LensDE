/** Paper allocation and the small set of editable initial-search controls.
 * Reading a saved task never installs a preset or rewrites hidden stage options.
 * β refers to trajectories/candidate capacities, independently of DE populations.
 */
import {clone,setAt} from './model.js';

export const allocationFields=[
 {id:'No',symbol:'Nₒ',key:'initial_search_funnel_global_explore_outer',paperDefault:5},
 {id:'Kc',symbol:'Kc',key:'initial_search_checkpoint_total_count',paperDefault:20},
 {id:'Kh',symbol:'Kh',key:'initial_search_checkpoint_handoff_count',paperDefault:7},
 {id:'Km',symbol:'Km',key:'initial_search_de_dls_material_entry_count',paperDefault:4},
];

export const searchAllocation=project=>Object.fromEntries(allocationFields.map(field=>[field.id,project.options[field.key]??null]));

export function allocationIssue(project){
 const beta=searchAllocation(project);
 if(Object.values(beta).some(value=>!Number.isSafeInteger(value)||value<1))return 'counts';
 return beta.Km<=beta.Kh&&beta.Kh<=beta.Kc?null:'order';
}

export function setSearchAllocation(project,id,value){
 const field=allocationFields.find(field=>field.id===id);
 if(!field||!Number.isSafeInteger(value)||value<1)throw Error('Allocation counts must be positive integers');
 // Allow an intermediate ordering while editing several fields. The view shows
 // the issue and the run action rejects it until Km <= Kh <= Kc is restored.
 return setAt(project,['options',field.key],value);
}

export function initialShapeMode(project){
 const o=project.options,mode=o.initial_search_staged_asphere_initial_mode;
 if(mode&&(!['spherical','conic'].includes(mode)||o.initial_search_post_asphere_enabled))return mode;
 if(o.initial_search_include_asphere&&String(o.initial_search_asphere_lens_indices??'').trim())return 'saved_asphere';
 return o.initial_search_include_conic?'conic':'spherical';
}

export function setInitialShapeMode(project,mode){
 if(!['spherical','conic'].includes(mode))throw Error('Unsupported initial shape mode');
 const next=clone(project),o=next.options;
 o.initial_search_staged_asphere_initial_mode=mode;
 o.initial_search_include_conic=mode==='conic';
 o.initial_search_include_asphere=false;
 // Explicit initial polynomial indices otherwise reactivate the old A4+ path
 // on normalization. Only an intentional shape change clears this field.
 o.initial_search_asphere_lens_indices='';
 return next;
}

// Same six values as the original workbench's connected-glass button. A
// preset changes the constraint recipe, never the user's surface libraries.
export const materialConstraintPresets={
 points:{initial_search_continuous_glass_penalty_enabled:true,
  initial_search_continuous_glass_penalty_weight:0.5,initial_search_continuous_glass_penalty_tolerance:1.5,
  initial_search_continuous_glass_region_enabled:false,
  initial_search_continuous_glass_region_weight:0.5,initial_search_continuous_glass_region_tolerance:0},
 envelope:{initial_search_continuous_glass_penalty_enabled:true,
  initial_search_continuous_glass_penalty_weight:0.05,initial_search_continuous_glass_penalty_tolerance:1.5,
  initial_search_continuous_glass_region_enabled:true,
  initial_search_continuous_glass_region_weight:0.5,initial_search_continuous_glass_region_tolerance:0},
};
export function materialConstraintScheme(project){
 return Object.entries(materialConstraintPresets).find(([,values])=>Object.entries(values).every(([key,value])=>
  typeof value==='boolean'?project.options[key]===value:
   typeof project.options[key]==='number'&&Math.abs(project.options[key]-value)<=1e-12))?.[0]??'custom';
}
export function setMaterialConstraintScheme(project,scheme){
 if(!Object.hasOwn(materialConstraintPresets,scheme))throw Error('Unsupported material constraint scheme');
 const next=clone(project);Object.assign(next.options,materialConstraintPresets[scheme]);
 if(next.options.initial_search_optimizer_mode==='de_dls_material_funnel')next.options.initial_search_run_profile='custom';
 return next;
}

export function searchBflReference(project){
 const last=Math.max(0,project.system.surfaces.length-2);
 const resolve=value=>{const n=Number(value);return value==null||!Number.isFinite(n)||n<0?last:Math.max(0,Math.min(last,Math.trunc(n)))};
 const saved=project.options.initial_search_bfl_reference_surface??-1;
 const system=resolve(project.system.bfl_reference_surface);
 return {saved,system,effective:Number(saved)>=0?resolve(saved):system,last};
}
export function setSearchBflReference(project,index){
 if(!Number.isInteger(index)||index< -1||index>searchBflReference(project).last)throw Error('Invalid BFL reference surface');
 return setAt(project,['options','initial_search_bfl_reference_surface'],index);
}

export function searchFieldContract(project){
 const o=project.options;
 return {coordinate:o.initial_search_post_asphere_field_coordinate??'chief_image_height',
  main:o.initial_search_post_asphere_fixed_main_field_count??o.initial_search_post_asphere_field_count??7,
  edges:Array.isArray(o.initial_search_post_asphere_fixed_edge_nodes)?o.initial_search_post_asphere_fixed_edge_nodes.length:
   String(o.initial_search_post_asphere_fixed_edge_nodes??'0.925,0.975,1').split(',').filter(s=>s.trim()).length};
}
