// Ordinary DE controls map to saved options; rendering never installs defaults
// into a benchmark. Paper workflow population preparation owns separate settings.
import {setAt,variableRows} from './model.js';

const classicStrategies=['best1bin','currenttobest1bin','rand1bin'];
const pupilKinds=new Set(['fvcx','fvcy','fvdx','fvdy','vignette_x','vignette_y','vignette_center_y','vignette_magnitude_y','vignette_asymmetry_y','vignetting_magnitude_y','vignetting_asymmetry_y']);

export function ordinaryDeSettings(project){
 const options=project.options;
 const strategy=options.de_algorithm==='jade'?'jade':options.de_strategy??'best1bin';
 const initialization=options.de_population_init??(options.initial_generation_mode==='physics_constrained'?'saved':'bounds');
 const dimension=Math.max(1,variableRows(project).filter(row=>!pupilKinds.has(row.kind)).length);
 return {strategy,initialization,scale:options.de_local_scale??'medium',dimension,
   population:Math.max(5,Math.max(1,Math.trunc(options.population??15))*dimension)};
}

export function setOrdinaryDeStrategy(project,strategy){
 if(!['jade',...classicStrategies].includes(strategy))throw Error('Unsupported DE strategy');
 const next=setAt(project,['options','de_algorithm'],strategy==='jade'?'jade':'classic');
 return strategy==='jade'?next:setAt(next,['options','de_strategy'],strategy);
}

export function setOrdinaryDeInitialization(project,initialization){
 if(!['local','bounds','saved'].includes(initialization))throw Error('Unsupported DE initialization');
 return setAt(project,['options','de_population_init'],initialization);
}

export function setOrdinaryDeScale(project,scale){
 if(!['tight','medium','wide'].includes(scale))throw Error('Unsupported local population scale');
 return setAt(project,['options','de_local_scale'],scale);
}
