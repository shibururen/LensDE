/** Existing executable sampling contracts; inspection never applies a preset.
 * Global/Checkpoint inherit one contract. Bridge, material accommodation and
 * Scout are deliberately linked by funnel.common.align_prescout_material_sampling.
 */
import {clone} from './model.js';

export const samplingStages=[
 {id:'global',zh:'全局 / Checkpoint',en:'Global / Checkpoint',prefixes:[]},
 {id:'material',zh:'Bridge / 适配 / Scout',en:'Bridge / accommodation / Scout',prefixes:['initial_search_de_dls_glass_scout_trace','initial_search_de_dls_material_bridge_trace']},
 {id:'chromatic',zh:'色差修复',en:'Chromatic repair',prefixes:['initial_search_de_dls_material_chromatic_repair_trace']},
 {id:'fixed',zh:'固定材料 DE / DLS',en:'Fixed-material DE / DLS',prefixes:['initial_search_de_dls_fixed_glass_feedback_trace']},
 {id:'asphere',zh:'多项式非球面',en:'Polynomial aspheres',prefixes:['initial_search_post_asphere_trace']},
];
export const samplingDistributions=['gaussian_ring_arm','gaussian_ring_arm_fixed_budget'];
const samplingKeys=[...new Set(samplingStages.flatMap(stage=>stage.prefixes.flatMap(prefix=>['rays','arms','distribution'].map(field=>prefix+'_'+field))))];
export const globalSamplingUsesMerit=project=>['full','full_merit','complete'].includes(project.options.initial_search_merit_strategy??'full');
export const meritSamplingKeys={rays:'num_rays',arms:'pupil_arms',distribution:'pupil_distribution'};

export function stageSampling(project,id){
 const stage=samplingStages.find(row=>row.id===id);
 if(!stage)throw Error('Unknown sampling stage');
 if(id==='global'&&globalSamplingUsesMerit(project))return Object.fromEntries(Object.entries(meritSamplingKeys).map(([field,key])=>[field,project.merit[key]]));
 if(id==='global')return Object.fromEntries(['rays','arms','distribution'].map(field=>[field,project.options['initial_search_trace_'+field]]));
 const prefix=stage.prefixes[0];
 return Object.fromEntries(['rays','arms','distribution'].map(field=>[field,project.options[prefix+'_'+field]]));
}

export function captureSampling(project){
 return {options:Object.fromEntries(samplingKeys.filter(key=>Object.hasOwn(project.options,key)).map(key=>[key,clone(project.options[key])])),
  merit:Object.fromEntries(['num_rays','pupil_arms','pupil_distribution'].filter(key=>Object.hasOwn(project.merit,key)).map(key=>[key,clone(project.merit[key])]))};
}

export function setStageSampling(project,id,field,value){
 const stage=samplingStages.find(row=>row.id===id);
 if(!stage||!['rays','arms','distribution'].includes(field))throw Error('Unknown sampling field');
 if(field==='distribution'?!samplingDistributions.includes(value):!Number.isInteger(value)||value<1)throw Error('Sampling counts must be positive integers and the pattern must be supported');
 const next=clone(project);
 if(id==='global'){
  if(!globalSamplingUsesMerit(project))throw Error('This imported legacy merit strategy controls its own global sampling');
  next.merit[meritSamplingKeys[field]]=value;return next;
 }
 for(const prefix of stage.prefixes)next.options[prefix+'_'+field]=value;
 return next;
}

export function applySamplingPlan(project,plan,baseline){
 if(plan==='custom')return clone(project);
 if(plan==='task_default'){
  if(!baseline)throw Error('No loaded-task sampling snapshot');
  const next=clone(project);
  for(const key of samplingKeys)delete next.options[key];
  Object.assign(next.options,clone(baseline.options));Object.assign(next.merit,clone(baseline.merit));
  return next;
 }
 if(!['paper_spherical','paper_aspherical'].includes(plan))throw Error('Unknown sampling plan');
 if(!globalSamplingUsesMerit(project))throw Error('Paper sampling plans require the full-merit workflow');
 const spherical=plan==='paper_spherical',distribution=spherical?'gaussian_ring_arm':'gaussian_ring_arm_fixed_budget';
 let next=clone(project);
 next.merit.num_rays=spherical?3:6;next.merit.pupil_arms=spherical?6:8;next.merit.pupil_distribution=distribution;
 for(const stage of samplingStages){
  const coarse=spherical&&!['fixed','asphere'].includes(stage.id);
  for(const [field,value] of Object.entries({rays:coarse?3:6,arms:coarse?6:8,distribution}))next=setStageSampling(next,stage.id,field,value);
 }
 return next;
}
