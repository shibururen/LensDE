import {state,$,tr,path} from './workbench-state.js';
import {getAt,variablePath,variableRows,isActiveJob,escapeHtml as h} from './model.js';
import {surfaceVariable,canToggleSurfaceVariable} from './surface-variables.js';
import {glassLibraryEntries,surfaceSearchLibraries,surfaceMaterialChoices,materialLibrary} from './glass-libraries.js';
import {meritSchema} from './merit-schema.js';
import {meritLabel,meritChoiceLabel,vignettingControlState} from './merit-labels.js';
import {samplingStages,samplingDistributions,globalSamplingUsesMerit,meritSamplingKeys} from './sampling-plans.js';
import {localize} from './workbench-labels.js';
import {ordinaryDeSettings} from './ordinary-de.js';
import {allocationFields,searchAllocation,allocationIssue,initialShapeMode,materialConstraintScheme,searchBflReference,searchFieldContract} from './initial-search-controls.js';

export function control(p,{type='number',nullable=false,choices=null,choiceLabel=String,readonly=false,data={},displayChecked=null}={}){
 const value=getAt(state.project,p),key=JSON.stringify(p),bad=state.invalid.get(key);
 const attrs=`data-path='${h(key)}' data-type="${type}" data-nullable="${nullable}" ${readonly?'disabled':''}`+Object.entries(data).map(([key,value])=>` data-${h(key)}="${h(value)}"`).join('');
 if(type==='boolean')return `<input type="checkbox" ${attrs} ${(displayChecked??(value===true))?'checked':''}>`;
 if(choices){const values=[...new Set([...(value!==undefined?[value]:[]),...choices])];return `<select ${attrs}>${value===undefined?'<option value=""></option>':''}${values.map(v=>`<option value="${h(v)}" ${v===value?'selected':''}>${h(choiceLabel(v))}</option>`).join('')}</select>`;}
 return `<input class="num" ${attrs} type="${type==='number'?'number':'text'}" ${type==='number'?'step="any"':''} value="${h(bad?.raw??value??'')}" ${bad?'aria-invalid="true"':''}>`;
}
const labelControl=(p,label,settings={})=>`<label>${h(label)} ${control(p,settings)}</label>`;
export function bindSidebar(){
 for(const input of document.querySelectorAll('[data-bind]')){
  const p=path(input.dataset.bind),value=getAt(state.project,p),bad=state.invalid.get(JSON.stringify(p));
  input.disabled=state.busy||getAt(state.project,p.slice(0,-1))==null;
  if(input.type==='checkbox')input.checked=value===true;
  else if(input.tagName==='SELECT'){
   if(![...input.options].some(o=>o.value===String(value??''))){const option=new Option(value===undefined?'':String(value),value===undefined?'':String(value));option.dataset.saved='true';input.prepend(option);}
   input.value=String(value??'');
  }else input.value=bad?.raw??value??'';
  input.classList.toggle('json-invalid',Boolean(bad));
 }
 const mode=state.ordinaryMode;
 const initial=state.optimizationScope==='initial';
 $('optimizationScope').value=state.optimizationScope;
 $('optimizationScope').disabled=state.busy;
 $('ordinaryOptimizer').value=mode;
 $('ordinaryOptimizer').disabled=state.busy;
 document.querySelectorAll('[data-ordinary-only]').forEach(el=>el.hidden=initial);
 document.querySelectorAll('[data-optimizer-panel]').forEach(el=>el.hidden=initial||!el.dataset.modes.split(' ').includes(mode));
 document.querySelectorAll('[data-paper-workflow]').forEach(el=>el.hidden=!initial);
 const de=ordinaryDeSettings(state.project),jade=de.strategy==='jade';
 $('ordinaryDeStrategy').value=de.strategy;
 const initialization=$('ordinaryDeInitialization');
 initialization.querySelector('[data-saved-initialization]')?.remove();
 if(!['bounds','local'].includes(de.initialization)){
  const saved=new Option(tr('保留工程初始化','Keep saved initialization'),de.initialization);
  saved.dataset.savedInitialization='true';initialization.prepend(saved);
 }
 initialization.value=de.initialization;
 $('ordinaryDeScale').value=de.scale;
 for(const id of ['ordinaryDeStrategy','ordinaryDeInitialization','ordinaryDeScale'])$(id).disabled=state.busy;
 document.querySelectorAll('[data-classic-de]').forEach(el=>el.hidden=jade);
 document.querySelectorAll('[data-jade-de]').forEach(el=>el.hidden=!jade);
 document.querySelectorAll('[data-de-local]').forEach(el=>el.hidden=de.initialization!=='local');
 $('ordinaryPopulationSummary').textContent=tr(`D=${de.dimension}（不计渐晕变量），共 ${de.population} 个体`,`D=${de.dimension} (excluding pupil variables), ${de.population} members`);
 $('ordinaryInitializationHint').textContent=de.initialization==='local'
  ?tr('保留当前结构；曲率保号扰动，其余变量按类型扰动并裁剪至初始化边界。','Keep the current prescription; perturb curvature magnitudes with signs preserved and other coordinates by type within initialization bounds.')
  :de.initialization==='bounds'?tr('在变量初始化边界内分层采样，并保留当前结构。','Stratified sampling over initialization bounds, retaining the current prescription.')
  :tr('沿用工程保存的种群生成设置。','Use the population generation settings saved in this project.');
 const run=$('runOptimize');
 run.dataset.zh='运行优化 · '+(initial?'初始搜索':mode==='local'?'DLS':jade?'JADE':'DE');
 run.dataset.en='Run optimization · '+(initial?'Initial search':mode==='local'?'DLS':jade?'JADE':'DE');
 renderSearchControls();
 renderGlassLibrarySummary();
 renderSamplingStages();
}
export function renderSearchControls(){
 const beta=searchAllocation(state.project);
 $('searchAllocationSummary').textContent=`β = (${allocationFields.map(field=>beta[field.id]??'—').join(', ')})`;
 const issue=allocationIssue(state.project),error=$('searchAllocationError');
 error.hidden=!issue;
 error.textContent=issue==='counts'?tr('β 各项必须为正整数。','Each β component must be a positive integer.')
  :issue==='order'?tr('候选上限须满足 Km ≤ Kh ≤ Kc。','Candidate capacities must satisfy Km ≤ Kh ≤ Kc.'):'';
 const select=$('initialShapeMode'),mode=initialShapeMode(state.project);
 select.querySelector('[data-saved-shape]')?.remove();
 if(!['spherical','conic'].includes(mode)){
  const option=new Option(tr('保留工程初始形式','Keep saved initial model')+(mode==='saved_asphere'?'':` (${mode.toUpperCase()})`),mode);
  option.dataset.savedShape='true';select.prepend(option);
 }
 select.value=mode;select.disabled=state.busy;
 const bfl=searchBflReference(state.project),bflControl=$('searchBflReference');
 bflControl.innerHTML=`<option value="-1">${h(tr('沿用系统','Follow system'))} · S${bfl.system}</option>`+
  Array.from({length:bfl.last+1},(_,i)=>`<option value="${i}">S${i} · ${h(state.project.system.surfaces[i]?.kind??'')}</option>`).join('');
 if(![...bflControl.options].some(o=>o.value===String(bfl.saved)))bflControl.prepend(new Option(tr('原配置','Saved')+` S${bfl.saved}`,String(bfl.saved)));
 bflControl.value=String(bfl.saved);bflControl.disabled=state.busy;
 $('searchBflSummary').textContent=tr(`初始搜索采用 S${bfl.effective} 至像面的轴向间隔之和。`,`Initial search measures the axial path from S${bfl.effective} to the image.`);
 document.querySelector('[data-search-wide-only]').hidden=state.project.options.initial_search_flow!=='wide_angle';
 const scheme=materialConstraintScheme(state.project),material=$('searchMaterialScheme'),o=state.project.options;
 material.querySelector('[data-custom-material]')?.remove();
 if(scheme==='custom'){const option=new Option(tr('保留工程自定义值','Keep custom project settings'),'custom');option.dataset.customMaterial='true';material.prepend(option);}
 material.value=scheme;material.disabled=state.busy;
 $('searchMaterialSummary').textContent=!o.initial_search_continuous_glass_penalty_enabled?tr('当前工程未启用材料邻近约束。','Material proximity is disabled in this project.'):
  tr(`点云权重 ${o.initial_search_continuous_glass_penalty_weight}，容差 ${o.initial_search_continuous_glass_penalty_tolerance}；`+
    (o.initial_search_continuous_glass_region_enabled?`包络权重 ${o.initial_search_continuous_glass_region_weight}，容差 ${o.initial_search_continuous_glass_region_tolerance}。`:'包络关闭。'),
   `Point weight ${o.initial_search_continuous_glass_penalty_weight}, tolerance ${o.initial_search_continuous_glass_penalty_tolerance}; `+
    (o.initial_search_continuous_glass_region_enabled?`envelope weight ${o.initial_search_continuous_glass_region_weight}, tolerance ${o.initial_search_continuous_glass_region_tolerance}.`:'envelope disabled.'));
 const fieldContract=searchFieldContract(state.project);
 $('searchFieldContractSummary').textContent=tr('阶段视场：','Stage fields: ')+
  (fieldContract.coordinate==='normalized_field'?tr('归一化视场','Normalized field'):tr('主光线像高','Chief-ray image height'))+
  tr(`；${fieldContract.main} 个主视场 + ${fieldContract.edges} 个边缘节点。`,`; ${fieldContract.main} main + ${fieldContract.edges} edge nodes.`);
 document.querySelector('[data-search-asphere-settings]').hidden=!state.project.options.initial_search_post_asphere_enabled;
}
function renderSamplingStages(){
 const plans=[['task_default',tr('保留任务原配置','Loaded task settings')],['paper_spherical',tr('球面：3×6 → 6×8','Spherical: 3×6 → 6×8')],['paper_aspherical',tr('非球面：全程 6×8','Aspheric: 6×8 throughout')],['custom',tr('自定义','Custom')]];
 $('samplingPlanPreset').innerHTML=plans.map(([id,label])=>`<option value="${id}" ${state.samplingPlan===id?'selected':''}>${h(label)}</option>`).join('');
 $('samplingStageTable').innerHTML=`<thead><tr>${[tr('阶段','Stage'),tr('方式','Pattern'),tr('环','Rings'),tr('臂','Arms')].map(label=>`<th>${h(label)}</th>`).join('')}</tr></thead><tbody>${samplingStages.map(stage=>{
  const prefix=stage.prefixes[0],inactive=stage.id==='asphere'&&!state.project.options.initial_search_post_asphere_enabled;
  return `<tr><td>${h(state.locale==='zh-CN'?stage.zh:stage.en)}${inactive?` <span class="muted">${h(tr('（未启用）','(inactive)'))}</span>`:''}</td>${['distribution','rays','arms'].map(field=>{
   const p=stage.id==='global'?(globalSamplingUsesMerit(state.project)?['merit',meritSamplingKeys[field]]:['options','initial_search_trace_'+field]):['options',prefix+'_'+field];
   return `<td>${control(p,{type:field==='distribution'?'text':'number',choices:field==='distribution'?samplingDistributions:null,readonly:stage.id==='global'&&!globalSamplingUsesMerit(state.project),data:{'sampling-stage':stage.id,'sampling-field':field}})}</td>`;
  }).join('')}</tr>`;
 }).join('')}</tbody>`;
}
export function renderGlassLibrarySummary(){
 const entries=glassLibraryEntries(state.project);
 $('glassLibrarySummary').innerHTML=entries.length?entries.map(row=>{
  const title=[row.searchSurfaces.length?tr('优化候选：','Optimization: ')+row.searchSurfaces.map(index=>'S'+index).join(', '):'',row.materialSurfaces.length?tr('当前材料：','Assigned material: ')+row.materialSurfaces.map(index=>'S'+index).join(', '):''].filter(Boolean).join(' · ');
  return `<li title="${h(title)}">${h(row.name)}</li>`;
 }).join(''):`<li class="empty">${h(tr('尚未配置材料搜索库','No material search libraries configured'))}</li>`;
}
function glassMaterialOptions(index){
 const m=state.project.system.surfaces[index].material_after??{};
 return `<option value="">${h(m.name??'')} / ${h(materialLibrary(m))}</option>${surfaceMaterialChoices(state.project,index,state.materials).map(({record:row,index:gi})=>`<option value="${gi}">${h(row.name)} / ${h(materialLibrary(row))} nᵈ ${h(row.nd)} Vᵈ ${h(row.vd)}</option>`).join('')}`;
}
export function refreshGlassMaterialChoices(){
 for(const selector of document.querySelectorAll('[data-glass-surface]')){
  selector.innerHTML=glassMaterialOptions(Number(selector.dataset.glassSurface));
  selector.disabled=state.busy||selector.options.length<=1;
 }
}
function surfaceParameter(index,field,kind,{term=0,...settings}={}){
 const selected=Boolean(surfaceVariable(state.project,kind,index,term));
 const enabled=canToggleSurfaceVariable(state.project,kind,index,term);
 const name=kind==='asphere_coeff'?`A${2*(term+1)}`:kind==='curvature'?tr('曲率（1/R）','Curvature (1/R)'):field;
 const title=tr(`表面 ${index} · ${name}：设为优化变量`,`Surface ${index} · ${name}: optimize this parameter`);
 const p=['system','surfaces',index,...(kind==='asphere_coeff'?['asphere_coefficients',term]:kind.startsWith('material_')?['material_after',field]:[field])];
 return `<div class="param-cell">${control(p,settings)}<label class="var-flag${selected?' is-variable':''}" title="${h(title)}"><input type="checkbox" class="var-toggle" data-surface-variable="${kind}" data-variable-surface="${index}" data-variable-term="${term}" aria-label="${h(title)}" ${selected?'checked':''} ${!enabled||state.busy?'disabled':''}>V</label></div>`;
}
function glassCell(surface,index,opened=false){
 const m=surface.material_after??{},cfgIndex=state.project.system.glass_search?.surface_configs?.findIndex(row=>Number(row.surface_index)===index)??-1;
 const libraryOptions=selected=>[...new Set([...selected,...state.libraries.map(row=>row.name)])].map(name=>`<option value="${h(name)}" ${selected.includes(name)?'selected':''}>${h(name)}</option>`).join('');
 let out=`<div class="material-cell">${control(['system','surfaces',index,'material_after','mode'],{type:'text',choices:['air','glass','abbe']})}`;
 if(m.mode==='abbe')out+=`<div class="abbe-row">nᵈ ${surfaceParameter(index,'nd','material_nd')} Vᵈ ${surfaceParameter(index,'vd','material_vd')}</div>`;
 else if(m.mode==='glass')out+=`<select data-glass-surface="${index}">${glassMaterialOptions(index)}</select><div class="material-meta">${h(m.filename??m.source_path??'')}</div>`;
 if((cfgIndex>=0||m.mode!=='air')&&!['object','image'].includes(surface.kind)){
  const libraries=surfaceSearchLibraries(state.project,index);
  out+=`<details class="surface-policy" ${opened?'open':''}><summary>${h(tr('优化搜索库','Optimization libraries'))} · ${h(libraries.join(', '))}</summary><select multiple size="4" aria-label="${h(tr('本面优化搜索库','Surface optimization libraries'))}" data-library-surface="${index}">${libraryOptions(libraries)}</select></details>`;
 }
 return out+'</div>';
}
export function renderSurfaces(){
 const opened=new Set([...document.querySelectorAll('details.surface-policy[open]')].map(element=>Number(element.closest('[data-surface-index]').dataset.surfaceIndex)));
 renderGlassLibrarySummary();
 const headers=['#',tr('类型','Kind'),'R','Conic',tr('厚度','Thickness'),tr('通光半径','Clear radius'),tr('通光固定','Fixed clear'),tr('机械半径','Mechanical radius'),tr('机械固定','Fixed mechanical'),'Stop',tr('反射','Reflective'),tr('材料','Material'),...Array.from({length:8},(_,i)=>`A${(i+1)*2}`)];
 $('surfaceTable').innerHTML=`<thead><tr>${headers.map(s=>`<th>${h(s)}</th>`).join('')}</tr></thead><tbody>${state.project.system.surfaces.map((s,i)=>{const p=['system','surfaces',i];return `<tr data-surface-index="${i}" class="${i===state.selectedSurface?'selected':''}"><td>${i}</td><td>${control([...p,'kind'],{type:'text',choices:['object','standard','plane','even_asphere','image']})}</td>${['radius','conic','thickness','semi_aperture','clear_aperture_fixed','mechanical_aperture','mechanical_aperture_fixed','is_stop','reflective'].map(k=>`<td>${['radius','conic','thickness'].includes(k)?surfaceParameter(i,k,k==='radius'?'curvature':k,{nullable:['radius','thickness'].includes(k)}):control([...p,k],{type:['clear_aperture_fixed','mechanical_aperture_fixed','is_stop','reflective'].includes(k)?'boolean':'number'})}</td>`).join('')}<td>${glassCell(s,i,opened.has(i))}</td>${Array.from({length:8},(_,ci)=>`<td>${surfaceParameter(i,'asphere_coefficients','asphere_coeff',{term:ci,readonly:s.kind!=='even_asphere'||!Array.isArray(s.asphere_coefficients)})}</td>`).join('')}</tr>`;}).join('')}</tbody>`;
}
function renderVariables(){const rows=variableRows(state.project),base=variablePath(state.project);$('variableTable').innerHTML=`<thead><tr>${['#',tr('变量类型','Kind'),tr('索引','Index'),tr('项','Term'),tr('下限','Lower'),tr('上限','Upper'),tr('初值','Initial'),tr('备注','Role')].map(x=>`<th>${h(x)}</th>`).join('')}</tr></thead><tbody>${rows.map((v,i)=>`<tr data-variable-index="${i}" class="${i===state.selectedVariable?'selected':''}"><td>${i}</td>${['kind','index','term','lower','upper','initial','role'].map(k=>`<td>${control([...base,i,k],{type:['kind','role'].includes(k)?'text':'number',choices:k==='kind'?['curvature','conic','thickness','material_nd','material_vd','asphere_coeff','glass_choice','fvcy','fvdy','vignette_x','vignette_y']:null,readonly:k==='term'&&v.kind!=='asphere_coeff'})}</td>`).join('')}</tr>`).join('')}</tbody>`;}
function renderFields(){
 $('fieldTable').innerHTML=`<thead><tr>${['#','hx','hy','FVCX','FVCY','FVDX','FVDY',''].map(x=>`<th>${x}</th>`).join('')}</tr></thead><tbody>${state.project.system.fields.map((row,i)=>`<tr data-field-index="${i}" class="${i===state.selectedField?'selected':''}"><td>${i}</td>${['hx','hy','fvcx','fvcy','fvdx','fvdy'].map(k=>`<td>${control(['system','fields',i,k])}</td>`).join('')}<td><button data-delete-field="${i}" class="danger mini">${h(tr('删','Delete'))}</button></td></tr>`).join('')}</tbody>`;
 $('waveTable').innerHTML=`<thead><tr><th>#</th><th>${h(tr('波长 μm','Wavelength μm'))}</th><th>${h(tr('权重','Weight'))}</th><th>${h(tr('主波长','Primary'))}</th><th></th></tr></thead><tbody>${state.project.system.wavelengths.map((w,i)=>`<tr><td>${i}</td><td>${control(['system','wavelengths',i,'value_um'])}</td><td>${control(['system','wavelengths',i,'weight'])}</td><td>${control(['system','wavelengths',i,'primary'],{type:'boolean'})}</td><td><button data-delete-wave="${i}" class="danger mini">${h(tr('删','Delete'))}</button></td></tr>`).join('')}</tbody>`;
}
const meritChoices={merit_model:meritSchema.meritModelOptions.map(r=>r[0]),distortion_model:meritSchema.distortionModelOptions.map(r=>r[0]),image_quality_metric:meritSchema.imageQualityMetricOptions.map(r=>r[0]),wavefront_reference:meritSchema.wavefrontReferenceOptions.map(r=>r[0])};
function recordTable(key,rows,readonly=false){
 const keys=[...new Set(rows.flatMap(row=>Object.keys(row)))];if(!keys.length)keys.push(key==='operands'?'type':'kind','target','weight');
 return `<div class="table-wrap"><table><thead><tr><th>#</th>${keys.map(k=>`<th>${h(k)}</th>`).join('')}<th></th></tr></thead><tbody>${rows.map((row,i)=>`<tr><td>${i}</td>${keys.map(k=>`<td>${readonly?h(typeof row[k]==='object'?JSON.stringify(row[k]):row[k]):control(['merit',key,i,k],{type:typeof row[k]==='number'?'number':'text',readonly:typeof row[k]==='object'})}</td>`).join('')}<td>${readonly?'':`<button data-remove-record="${key}" data-index="${i}" class="danger mini">${h(tr('删','Delete'))}</button>`}</td></tr>`).join('')}</tbody></table></div>`;
}
export function renderMerit(){
 const m=state.project.merit,vignetting=vignettingControlState(state.project);
 const unified=['unified_trace_gate','trace_guarded_adaptive','unified_adaptive','trace_guarded'].includes(m.merit_model);
 const order=[...new Set([...(m.auto_merit?meritSchema.autoKeys:meritSchema.manualKeys).map(r=>r[0]),...(unified?meritSchema.unifiedKeys.map(r=>r[0]):[])])];
 const first=['auto_merit','merit_model','chromatic_spot_merit_enabled','distortion_model','image_quality_metric','wavefront_reference'];
 const fields=[...first,...order.filter(k=>!first.includes(k))].filter(k=>!['operands','bounds','vignetting_physical_cut_match_enabled'].includes(k)&&typeof m[k]!=='object');
 fields.splice(fields.indexOf('minimum_relative_illumination')+1,0,'vignetting_physical_cut_match_enabled');
 const firstThickness=fields.find(k=>/^(glass|air)_(center|edge)_thickness_(lower|upper)$/.test(k));
 const thicknessHelp=`<p id="meritThicknessHelp" class="form-note" role="note">${h(tr('制造厚度边界用于评价与筛选；初始结构搜索使用完整评价函数时，中心厚度边界还参与 TTL/BFL 预算和采样范围计算。','Manufacturing thickness limits are used in evaluation and screening. In full-merit initial search, center-thickness limits also enter the TTL/BFL budget and sampling-range calculation.'))}</p>`;
 const inputs=fields.map(k=>{
  const match=k==='vignetting_physical_cut_match_enabled';
  const settings={type:match||typeof m[k]==='boolean'?'boolean':typeof m[k]==='number'?'number':'text',
   choices:meritChoices[k],choiceLabel:value=>meritChoiceLabel(k,value,state.locale),
   readonly:state.busy||(['vignetting_physical_cut_match_enabled','vignetting_boundary_support_weight'].includes(k)&&!vignetting.enabled)};
  if(match)settings.displayChecked=vignetting.physicalMatchEffective;
  const help=match?`<p id="meritVignettingHelp" class="form-note" role="note">${h(tr('最小相对照度为 0 时，照度约束与渐晕优化关闭，本项不生效；设为正值后，沿用工程保存的物理孔径残差选择。','At minimum relative illumination = 0, illumination constraints and vignetting optimization are off, so this option is inactive. With a positive value, the saved physical-aperture residual selection applies.'))}</p>`:'';
  return (k===firstThickness?thicknessHelp:'')+labelControl(['merit',k],meritLabel(k,state.locale),settings)+help;
 }).join('');
 const operands=m.auto_merit?(state.meritPreview?.operands??m.operands??[]):m.operands??[];
 const operandDraft=state.invalid.get(JSON.stringify(['merit','operands']))?.raw;
 const boundsDraft=state.invalid.get(JSON.stringify(['merit','bounds']))?.raw;
 $('meritGrid').innerHTML=`${inputs}<label class="json-field">${h(m.auto_merit?tr('自动生成 operands JSON','Automatically generated operands JSON'):'operands JSON')}<textarea data-merit-json="operands" spellcheck="false" ${m.auto_merit?'readonly':''}>${h(operandDraft??JSON.stringify(operands,null,2))}</textarea></label><label class="json-field">bounds JSON<textarea data-merit-json="bounds" spellcheck="false">${h(boundsDraft??JSON.stringify(m.bounds??[],null,2))}</textarea></label>`;
}
export function updateJobView(){const job=state.job;$('jobId').value=state.jobId;$('jobStatus').textContent=job?`${job.status} · ${job.progress?.stage??''}${job.progress?.generation!==undefined?' · G'+job.progress.generation:''}`:'';$('logBox').textContent=job?.log??'';$('stopJob').disabled=!isActiveJob(job);$('resumeJob').disabled=!state.jobId||isActiveJob(job);$('scoreBox').textContent=JSON.stringify(job?.result?.score??{},null,2);if($('applyResult'))$('applyResult').disabled=!job?.result?.project;}
export function render(){
 if(!state.project)return;bindSidebar();renderSurfaces();renderVariables();renderFields();renderMerit();localize();
 document.querySelectorAll('.tab').forEach(el=>el.classList.toggle('active',el.dataset.tab===state.tab));
 document.querySelectorAll('.workspace>.panel').forEach(el=>el.classList.toggle('active',el.id===state.tab));
 $('app').setAttribute('aria-busy',String(state.busy));updateJobView();
}
