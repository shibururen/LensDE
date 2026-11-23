/** Surface-row variable flags share the saved variable list with the editor.
 * Reading a flag never changes a prescription or its existing bounds. Only an
 * explicit toggle adds/removes a coordinate, using the original workbench's
 * default bounds for newly created variables.
 */
import {clone,setAt,variablePath,variableRows} from './model.js';

const kinds=new Set(['curvature','conic','thickness','material_nd','material_vd','asphere_coeff']);
const sagBudgets=[.10,.30,.35,.20,.10,.06,.04,.02];
const finite=(value,fallback=0)=>Number.isFinite(value)?value:fallback;

function matches(variable,kind,index,term){
 return (variable.kind===kind||(kind==='curvature'&&variable.kind==='radius'))&&
  Number(variable.index)===Number(index)&&(kind!=='asphere_coeff'||Number(variable.term??0)===Number(term));
}

export function surfaceVariable(project,kind,index,term=0){
 return variableRows(project).find(v=>matches(v,kind,index,term));
}

export function canToggleSurfaceVariable(project,kind,index,term=0){
 const surface=project.system.surfaces[index];
 if(!surface||!kinds.has(kind))return false;
 if(kind==='asphere_coeff'&&(!Number.isInteger(term)||term<0||term>=8))return false;
 if(surfaceVariable(project,kind,index,term))return true; // Allow removing a saved coordinate even after a mode change.
 if(['object','image'].includes(surface.kind))return false;
 if(kind.startsWith('material_'))return surface.material_after?.mode==='abbe';
 return kind!=='thickness'||Number.isFinite(surface.thickness);
}

function currentValue(surface,kind,term){
 if(kind==='curvature')return surface.radius==null||surface.radius===0?0:1/surface.radius;
 if(kind==='material_nd')return finite(surface.material_after?.nd,1);
 if(kind==='material_vd')return finite(surface.material_after?.vd);
 if(kind==='asphere_coeff')return finite(surface.asphere_coefficients?.[term]);
 return finite(surface[kind]);
}

function defaultBounds(project,kind,index,term,value){
 const surface=project.system.surfaces[index],material=surface.material_after??{};
 if(kind==='curvature')return {lower:-.5,upper:.5};
 if(kind==='conic')return {lower:-100,upper:100};
 if(kind==='material_nd')return {lower:1.25,upper:2.3};
 if(kind==='material_vd')return {lower:25,upper:100};
 if(kind==='thickness')return material.mode==='glass'||(material.mode==='abbe'&&material.nd>1.0005)
  ?{lower:.5,upper:10}:{lower:.1,upper:100};
 const aperture=project.system.aperture??{},pupil=finite(aperture.value);
 const fallback=pupil>0?(String(aperture.type).toUpperCase()==='EPD'?pupil*.5:pupil):1;
 const radius=Math.max(1,fallback,...[surface.semi_aperture,surface.aperture_radius,surface.mechanical_aperture,surface.aperture?.r_max].map(v=>Math.abs(finite(v))));
 const budget=term===0&&surfaceVariable(project,'curvature',index)? .03:sagBudgets[term];
 const span=Math.max(Math.abs(value)*10,budget/Math.max(radius**(2*(term+1)),1));
 return {lower:value-span,upper:value+span};
}

export function setSurfaceVariable(project,kind,index,enabled,term=0){
 if(!canToggleSurfaceVariable(project,kind,index,term))throw Error('This surface parameter cannot be an optimization variable');
 const rows=variableRows(project),existing=surfaceVariable(project,kind,index,term);
 if(!enabled)return setAt(project,variablePath(project),rows.filter(v=>!matches(v,kind,index,term)));
 if(existing)return clone(project); // Keep legacy radius coordinates and all user-defined bounds/metadata intact.
 const value=currentValue(project.system.surfaces[index],kind,term);
 const role=kind==='asphere_coeff'?`A${2*(term+1)} @ S${index}`:`${kind} @ S${index}`;
 const next=setAt(project,variablePath(project),[...rows,{kind,index,term:kind==='asphere_coeff'?term:0,...defaultBounds(project,kind,index,term,value),initial:value,role}]);
 if(kind==='asphere_coeff'){
  const surface=next.system.surfaces[index];
  surface.kind='even_asphere';
  surface.asphere_coefficients??=[];
  while(surface.asphere_coefficients.length<=term)surface.asphere_coefficients.push(0);
 }
 return next;
}

export function setVariableBound(project,rowIndex,key,value){
 if(!['lower','upper'].includes(key)||!Number.isFinite(value))throw Error('Invalid variable bound');
 const next=setAt(project,[...variablePath(project),rowIndex,key],value);
 const variable=variableRows(next)[rowIndex];
 if(!(variable.lower<variable.upper))throw Error('The lower bound must be less than the upper bound');
 // The normalizer reads matching merit bounds after the variable list. An
 // explicit bound edit must update that same constraint, or Save/Run would
 // silently restore the old range. Other variables and constraints stay intact.
 const bounds=next.merit.bounds?.length?next.merit.bounds:next.merit.variable_bounds??next.merit.bounds??[];
 const matching=bounds.filter(bound=>bound.kind===variable.kind&&Number(bound.index)===Number(variable.index)&&
  (variable.kind!=='asphere_coeff'||Number(bound.term??0)===Number(variable.term??0)));
 for(const bound of matching){
  bound.lower=variable.lower;bound.upper=variable.upper;
  if(bound.source==='automatic_conic_coefficient_penalty')bound.source='user_variable_bounds';
 }
 if(!matching.length&&variable.kind==='conic'&&next.merit.conic_coefficient_penalty_enabled!==false&&next.merit.conic_coefficient_penalty_weight>0){
  (next.merit.bounds??=[]).push({kind:'conic',index:variable.index,term:0,lower:variable.lower,upper:variable.upper,weight:next.merit.conic_coefficient_penalty_weight,source:'user_variable_bounds'});
 }
 return next;
}
