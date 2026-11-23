import {api} from './api.js';
import {clone,getAt,setAt,parseNumeric,variablePath,variableRows,scientificSnapshot,jobPayload,optimizationPayload,selectOptimization,shouldApplyCompletedResult,normalizeJob,isActiveJob,safeLocalUrl,catalogMaterial,escapeHtml as h} from './model.js';
import {loadSpotData,renderSpotDiagrams} from './plots.js';
import {setSurfaceVariable,setVariableBound} from './surface-variables.js';
import {systemGlassLibraries,setSurfaceSearchLibraries,surfaceSearchLibraries} from './glass-libraries.js';
import {applySamplingPlan,setStageSampling,meritSamplingKeys} from './sampling-plans.js';
import {setOrdinaryDeStrategy,setOrdinaryDeInitialization,setOrdinaryDeScale} from './ordinary-de.js';
import {setSearchAllocation,setInitialShapeMode,allocationIssue,setSearchBflReference,setMaterialConstraintScheme} from './initial-search-controls.js';
import {state,$,tr,path,guard,modified,accept,notice} from './workbench-state.js';
import {render,renderMerit,renderSurfaces,updateJobView,renderGlassLibrarySummary,refreshGlassMaterialChoices,renderSearchControls} from './workbench-view.js';

let previewTimer;
function scheduleMeritPreview(){
 clearTimeout(previewTimer);if(!state.project?.merit.auto_merit||state.invalid.size)return;
 const revision=state.sourceRevision;
 previewTimer=setTimeout(async()=>{try{const response=await api.meritPreview(scientificSnapshot(state.project));if(state.sourceRevision!==revision)return;state.meritPreview=clone(response.effective_merit??response.merit);const editor=document.querySelector('[data-merit-json="operands"]');if(editor?.readOnly)editor.value=JSON.stringify(state.meritPreview.operands??[],null,2);}catch(error){notice(error.message,true);}},250);
}

async function busy(callback){
 if(state.busy)return;state.busy=true;$('app').setAttribute('aria-busy','true');document.querySelectorAll('.top-actions button,#optimizationScope,#ordinaryOptimizer').forEach(el=>el.disabled=true);
 try{await callback();}catch(error){notice(error.message,true);}finally{state.busy=false;render();scheduleMeritPreview();document.querySelectorAll('.top-actions button').forEach(el=>el.disabled=false);}
}
let spotResult, spotRender = 0;

async function plotSpots() {
 const result = spotResult, revision = ++spotRender;
 const summary = $('spotSummary');
 if (!result) {
  summary.textContent = tr('运行追迹后显示','Run a trace to display results');
  return;
 }
 if (!$('spotGrid').childElementCount) summary.textContent = tr('正在绘制点列图…','Drawing spot diagrams…');
 try {
  const data = await result.data;
  if (revision !== spotRender || result.jobId !== state.jobId) return;
  renderSpotDiagrams($('spotGrid'),$('spotLegend'),data,{
   field:tr('视场','Field'),
   rays:tr('条光线','rays'),
   rmsInfo:tr('以绿光加权质心为参考，各颜色的加权 RMS 取平均。','RMS is averaged across color bands about the weighted green-light centroid.'),
   scaleMode:$('spotScale').value,
  });
  const count = data.groups.length;
  summary.textContent = count
   ? (state.locale === 'zh-CN' ? count + ' 个视场' : count + (count === 1 ? ' field' : ' fields'))
   : tr('没有可绘制的有效光线','No valid spot rays to plot');
 } catch (error) {
  if (revision !== spotRender || result.jobId !== state.jobId) return;
  spotResult = undefined;
  summary.textContent = tr('无法绘制点列图','Unable to draw spot diagrams') + ' · ' + error.message;
 }
}

function plots() {
 const result = state.job?.result;
 if (!result) {clearPlots();return;}
 const layout = safeLocalUrl(result.layout_url,location.origin);
 if (layout) {
  if ($('layoutObject').getAttribute('data') !== layout) $('layoutObject').data = layout;
  $('downloadLayout').href = layout;
 } else {
  $('layoutObject').removeAttribute('data');
  $('downloadLayout').removeAttribute('href');
 }
 const artifacts = result.artifacts ?? [];
 for (const [id,file] of [['downloadRaytrace','raytrace.csv'],['downloadSpot','spot.csv']]) {
  const item = artifacts.find(record => record.name === file);
  const url = safeLocalUrl(item?.url,location.origin);
  if (url) $(id).href = url;
  else $(id).removeAttribute('href');
 }
 const source = safeLocalUrl(result.spot_url,location.origin);
 if (source !== spotResult?.source || spotResult?.jobId !== state.jobId) {
  $('spotGrid').replaceChildren();
  $('spotLegend').replaceChildren();
  $('spotScale').disabled = !source;
  $('spotScale').value = result.project?.options?.spot_scale_mode === 'fit' ? 'fit' : 'shared';
  spotResult = source ? {
   source, jobId:state.jobId,
   data:loadSpotData(source,result.system?.wavelengths ?? result.project?.system?.wavelengths),
  } : undefined;
 }
 void plotSpots();
}

function clearPlots() {
 spotResult = undefined;
 spotRender++;
 $('layoutObject').removeAttribute('data');
 $('spotGrid').replaceChildren();
 $('spotLegend').replaceChildren();
 $('spotScale').disabled = true;
 $('spotSummary').textContent = tr('运行追迹后显示','Run a trace to display results');
 for (const id of ['downloadLayout','downloadRaytrace','downloadSpot']) $(id).removeAttribute('href');
}
async function refreshJob(){
 if(!state.jobId)return;const requestedId=state.jobId;const job=normalizeJob(await api.job(requestedId));if(requestedId!==state.jobId)return;state.job=job;updateJobView();
 if(isActiveJob(state.job)){clearTimeout(state.poll);state.poll=setTimeout(()=>refreshJob().catch(error=>notice(error.message,true)),650);}
 else{clearTimeout(state.poll);plots();
  if(shouldApplyCompletedResult(job,state.jobId,state.jobRevision,state.sourceRevision,state.appliedResultJob)){
   state.appliedResultJob=state.jobId;accept(job.result.project);render();scheduleMeritPreview();await loadGlasses();
  }else if(job.status==='completed'&&job.result?.project&&state.appliedResultJob!==state.jobId){
   state.appliedResultJob=state.jobId;notice(tr('设计已修改或切换，结果未覆盖当前项目；可选择应用结果。','The design changed; use Apply result project to load this result.'),true);
  }
 }
}
async function start(kind){
 guard();if(isActiveJob(state.job))throw Error(tr('请先停止当前任务。','Stop the current job first.'));
 if(kind==='optimize'&&state.optimizationScope==='initial'&&allocationIssue(state.project))throw Error(tr('请修正搜索分配 β：各项须为正整数，且 Km ≤ Kh ≤ Kc。','Correct β: use positive integers with Km ≤ Kh ≤ Kc.'));
 const payload=kind==='optimize'?optimizationPayload(state.project,{scope:state.optimizationScope,ordinaryMode:state.ordinaryMode}):jobPayload(state.project,kind);
 const response=await api.start(payload);state.jobId=response.job_id;state.job=null;state.jobProject=clone(payload.project);clearPlots();
 state.jobRevision=state.sourceRevision;state.appliedResultJob=null;if(kind!=='vignetting')state.tab='layout';await refreshJob();
}
let materialRequest=0;
async function loadGlasses({rerender=true}={}){
 const request=++materialRequest,names=systemGlassLibraries(state.project),key=JSON.stringify(names);
 const response=names.length?await api.materials('',names.join(',')):[];
 if(request!==materialRequest||key!==JSON.stringify(systemGlassLibraries(state.project)))return;
 state.materials=response.materials??response.glasses??response;
 if(rerender)renderSurfaces();else refreshGlassMaterialChoices();
}
function editSurfaceLibraries(event){
 const input=event.target;
 if(input.dataset.librarySurface===undefined||state.busy)return;
 try{
  const index=Number(input.dataset.librarySurface),selected=[...input.selectedOptions].map(option=>option.value);
  const next=setSurfaceSearchLibraries(state.project,index,selected);
  if(JSON.stringify(next.system.glass_search)===JSON.stringify(state.project.system.glass_search))return;
  modified(next);renderGlassLibrarySummary();refreshGlassMaterialChoices();
  input.closest('details').querySelector('summary').textContent=tr('优化搜索库','Optimization libraries')+' · '+surfaceSearchLibraries(next,index).join(', ');
  // Keep the live picker in place and accept subsequent choices while catalog
  // data loads. Only the latest response may refresh material dropdowns.
  loadGlasses({rerender:false}).catch(error=>notice(error.message,true));
 }catch(error){notice(error.message,true);}
}
document.addEventListener('input',editSurfaceLibraries);
document.addEventListener('change',editSurfaceLibraries);
function dialog(title,fields,submit){state.dialog={fields,submit};$('editTitle').textContent=title;$('editFields').innerHTML=fields.map(f=>`<label>${h(f.label??f.key)}<input name="${h(f.key)}" type="${f.type??'text'}" step="any" value="${h(f.value??'')}" ${f.required===false?'':'required'}></label>`).join('');$('editDialog').showModal();}
const fieldKinds=new Set(['fvcx','fvcy','fvdx','fvdy','vignette_x','vignette_y','vignette_magnitude_y','vignette_asymmetry_y','vignetting_magnitude_y','vignetting_asymmetry_y']);
function removeField(index){const next=clone(state.project);next.system.fields.splice(index,1);const rows=variableRows(next).filter(v=>!fieldKinds.has(v.kind)||v.index!==index).map(v=>fieldKinds.has(v.kind)&&v.index>index?{...v,index:v.index-1}:v);modified(setAt(next,variablePath(next),rows));state.selectedField=Math.max(0,index-1);render();}
async function exportCurrent(format){guard();const response=await api.export(scientificSnapshot(state.project),format);const url=safeLocalUrl(response.download_url??response.url,location.origin);if(!url)throw Error('Missing same-origin export URL');const a=document.createElement('a');a.href=url;a.download='';a.click();}
const handlers={
 editWorkflowSampling:()=>{guard();$('workflowSamplingDialog').showModal();},
 runTrace:()=>start('trace'),runOptimize:()=>start('optimize'),
 newProject:async()=>{guard();accept(await api.defaultProject());await loadGlasses();},
 loadBenchmark:async()=>{guard();accept(await api.benchmark($('benchmarkSelect').value));await loadGlasses();},
 loadProject:()=>{$('projectFile').dataset.importKind='project';$('projectFile').click();},
 importOptiland:()=>{$('projectFile').dataset.importKind='system';$('projectFile').click();},
 saveProject:()=>exportCurrent('json'),exportSystem:()=>exportCurrent('system'),exportCodeV:()=>exportCurrent('seq'),exportZemax:()=>exportCurrent('zmx'),
 stopJob:async()=>{await api.stop(state.jobId);await refreshJob();},
 resumeJob:async()=>{guard();await api.resume(state.jobId,state.jobProject??undefined);await refreshJob();},
 refreshJob:async()=>{const id=$('jobId').value.trim();if(id!==state.jobId){state.jobProject=null;state.jobRevision=null;}state.jobId=id;await refreshJob();},
 applyResult:async()=>{guard();if(state.job?.result?.project){accept(state.job.result.project);state.tab='surfaces';await loadGlasses();}},
 setVignetting:()=>start('vignetting'),
 clearVignetting:()=>{const next=clone(state.project);for(const f of next.system.fields)for(const k of ['fvcx','fvcy','fvdx','fvdy','vignette_x','vignette_y'])if(k in f||['fvcx','fvcy','fvdx','fvdy'].includes(k))f[k]=0;for(const v of variableRows(next)){const f=next.system.fields[v.index];if(f&&['fvcx','fvcy','fvdx','fvdy','vignette_x','vignette_y'].includes(v.kind))v.initial=f[v.kind];}modified(next);},
 addVariable:()=>dialog(tr('添加变量','Add variable'),['kind','index','term','lower','upper','initial','role'].map(k=>({key:k,type:['kind','role'].includes(k)?'text':'number',required:!['term','role'].includes(k)})),values=>{const row={kind:values.kind,index:parseNumeric(values.index),lower:parseNumeric(values.lower),upper:parseNumeric(values.upper),initial:parseNumeric(values.initial)};if(values.term.trim())row.term=parseNumeric(values.term);if(values.role.trim())row.role=values.role;const rows=clone(variableRows(state.project));rows.push(row);modified(setAt(state.project,variablePath(state.project),rows));}),
 deleteVariable:()=>{const rows=clone(variableRows(state.project));rows.splice(state.selectedVariable,1);modified(setAt(state.project,variablePath(state.project),rows));},
 addField:()=>dialog(tr('添加视场','Add field'),['hx','hy','fvcx','fvcy','fvdx','fvdy'].map(key=>({key,type:'number'})),values=>{const next=clone(state.project);next.system.fields.push(Object.fromEntries(Object.entries(values).map(([k,v])=>[k,parseNumeric(v)])));modified(next);}),
 deleteField:()=>removeField(state.selectedField),
 addWave:()=>dialog(tr('添加波长','Add wavelength'),[{key:'value_um',label:tr('波长 μm','Wavelength μm'),type:'number'},{key:'weight',label:tr('权重','Weight'),type:'number'}],values=>{const next=clone(state.project);next.system.wavelengths.push({value_um:parseNumeric(values.value_um),weight:parseNumeric(values.weight),primary:false});modified(next);}),
 duplicateSurface:()=>{const next=clone(state.project),index=state.selectedSurface+1;next.system.surfaces.splice(index,0,clone(next.system.surfaces[state.selectedSurface]));for(const v of variableRows(next))if(!fieldKinds.has(v.kind)&&v.index>=index)v.index++;for(const cfg of next.system.glass_search?.surface_configs??[])if(cfg.surface_index>=index)cfg.surface_index++;modified(next);state.selectedSurface=index;},
 deleteSurface:()=>{const index=state.selectedSurface,next=clone(state.project);next.system.surfaces.splice(index,1);const rows=variableRows(next).filter(v=>fieldKinds.has(v.kind)||v.index!==index).map(v=>!fieldKinds.has(v.kind)&&v.index>index?{...v,index:v.index-1}:v);if(next.system.glass_search)next.system.glass_search.surface_configs=(next.system.glass_search.surface_configs??[]).filter(row=>row.surface_index!==index).map(row=>row.surface_index>index?{...row,surface_index:row.surface_index-1}:row);modified(setAt(next,variablePath(next),rows));state.selectedSurface=Math.max(0,index-1);},
 previewMerit:async()=>{guard();const response=await api.meritPreview(scientificSnapshot(state.project));state.meritPreview=clone(response.effective_merit??response.merit);renderMerit();},
};
handlers.addSurface=handlers.duplicateSurface;
document.addEventListener('click',event=>{
 const tab=event.target.closest('[data-tab]');if(tab){state.tab=tab.dataset.tab;render();return;}
 const row=event.target.closest('[data-surface-index],[data-variable-index],[data-field-index]');if(row){if(row.dataset.surfaceIndex!==undefined)state.selectedSurface=Number(row.dataset.surfaceIndex);if(row.dataset.variableIndex!==undefined)state.selectedVariable=Number(row.dataset.variableIndex);if(row.dataset.fieldIndex!==undefined)state.selectedField=Number(row.dataset.fieldIndex);row.parentElement.querySelectorAll('tr').forEach(r=>r.classList.toggle('selected',r===row));}
 const button=event.target.closest('button');if(!button)return;
 if(button.id==='localeZh'||button.id==='localeEn'){state.locale=button.id==='localeEn'?'en':'zh-CN';localStorage.setItem('lensde.locale',state.locale);render();void plotSpots();return;}
 if(button.id==='cancelEdit'){state.dialog=null;$('editDialog').close();return;}
 if(button.id==='closeWorkflowSampling'){$('workflowSamplingDialog').close();return;}
 if(button.dataset.deleteField!==undefined){removeField(Number(button.dataset.deleteField));return;}
 if(button.dataset.deleteWave!==undefined){const next=clone(state.project);next.system.wavelengths.splice(Number(button.dataset.deleteWave),1);modified(next);render();return;}
 if(button.dataset.addRecord){const key=button.dataset.addRecord;dialog(tr('添加条目','Add record'),[{key:'json',label:'JSON'}],values=>{const record=JSON.parse(values.json);if(!record||Array.isArray(record)||typeof record!=='object')throw Error('Expected an object');const next=clone(state.project);(next.merit[key]??=[]).push(record);modified(next);});return;}
 if(button.dataset.removeRecord){const next=clone(state.project);next.merit[button.dataset.removeRecord].splice(Number(button.dataset.index),1);modified(next);render();return;}
 if(handlers[button.id]){event.preventDefault();busy(handlers[button.id]);}
});
document.addEventListener('change',event=>{
 const input=event.target;if(input.id==='spotScale'){void plotSpots();return;}if(state.busy)return;
 try{
  if(input.dataset.surfaceVariable){guard();const count=variableRows(state.project).length;const next=setSurfaceVariable(state.project,input.dataset.surfaceVariable,Number(input.dataset.variableSurface),input.checked,Number(input.dataset.variableTerm));modified(next);state.selectedVariable=variableRows(next).length>count?count:Math.min(state.selectedVariable,Math.max(0,variableRows(next).length-1));render();return;}
  if(input.id==='projectFile'&&input.files?.[0]){const file=input.files[0];busy(async()=>{const document=JSON.parse(await file.text());const response=await api.importProject(document,state.project);accept(response);$('filePath').value=file.name;await loadGlasses();});return;}
  if(input.id==='optimizationScope'){guard();const selection={scope:input.value,ordinaryMode:state.ordinaryMode};modified(selectOptimization(state.project,selection));state.optimizationScope=input.value;render();return;}
  if(input.id==='ordinaryOptimizer'){guard();const selection={scope:state.optimizationScope,ordinaryMode:input.value};modified(selectOptimization(state.project,selection));state.ordinaryMode=input.value;render();return;}
  if(input.id==='ordinaryDeStrategy'){guard();modified(setOrdinaryDeStrategy(state.project,input.value));render();return;}
  if(input.id==='ordinaryDeInitialization'){guard();modified(setOrdinaryDeInitialization(state.project,input.value));render();return;}
  if(input.id==='ordinaryDeScale'){guard();modified(setOrdinaryDeScale(state.project,input.value));render();return;}
  if(input.id==='initialShapeMode'){guard();modified(setInitialShapeMode(state.project,input.value));render();return;}
  if(input.id==='searchBflReference'){guard();modified(setSearchBflReference(state.project,Number(input.value)));render();return;}
  if(input.id==='searchMaterialScheme'){guard();modified(setMaterialConstraintScheme(state.project,input.value));render();return;}
  if(input.id==='samplingPlanPreset'){guard();modified(applySamplingPlan(state.project,input.value,state.samplingBaseline));state.samplingPlan=input.value;render();scheduleMeritPreview();return;}
  if(input.dataset.meritJson){const p=['merit',input.dataset.meritJson],value=JSON.parse(input.value);if(!Array.isArray(value))throw Error(tr('此处需要 JSON 数组。','A JSON array is required.'));state.invalid.delete(JSON.stringify(p));modified(setAt(state.project,p,value));scheduleMeritPreview();return;}
  if(input.dataset.librarySurface!==undefined)return;
  if(input.dataset.glassSurface!==undefined){if(input.value==='')return;modified(setAt(state.project,['system','surfaces',Number(input.dataset.glassSurface),'material_after'],catalogMaterial(state.materials[Number(input.value)])),{sync:true});render();return;}
  const p=input.dataset.path?JSON.parse(input.dataset.path):input.dataset.bind?path(input.dataset.bind):null;
  if(p){const previous=getAt(state.project,p),type=input.dataset.type??(input.type==='checkbox'?'boolean':input.type==='number'||typeof previous==='number'?'number':'text');const value=type==='boolean'?input.checked:type==='number'?parseNumeric(input.value,input.dataset.nullable==='true'):input.value;let next;
   if(input.dataset.searchCount&&(!Number.isSafeInteger(value)||value<Number(input.dataset.searchCount)))throw Error(tr('请输入不小于 '+input.dataset.searchCount+' 的整数。','Enter an integer of at least '+input.dataset.searchCount+'.'));
   if(input.dataset.searchAllocation){next=setSearchAllocation(state.project,input.dataset.searchAllocation,value);}
   else if(input.dataset.samplingStage){next=setStageSampling(state.project,input.dataset.samplingStage,input.dataset.samplingField,value);state.samplingPlan='custom';$('samplingPlanPreset').value='custom';}
   else if(p[0]==='system'&&p[1]==='surfaces'&&p[3]==='asphere_coefficients'){
    const coefficients=clone(state.project.system.surfaces[p[2]].asphere_coefficients);
    while(coefficients.length<=p[4])coefficients.push(0);
    coefficients[p[4]]=value;next=setAt(state.project,p.slice(0,-1),coefficients);
   }else if(p[0]==='variables'&&['lower','upper'].includes(p.at(-1)))next=setVariableBound(state.project,p.at(-2),p.at(-1),value);
   else next=setAt(state.project,p,value);
   if(p[0]==='system'&&p[1]==='wavelengths'&&p.at(-1)==='primary'&&value===true)next.system.wavelengths.forEach((w,i)=>{w.primary=i===p[2];});
   state.invalid.delete(JSON.stringify(p));input.classList.remove('json-invalid');input.setCustomValidity('');modified(next,{sync:p[0]==='system'&&['surfaces','fields'].includes(p[1])});
   if(p[0]==='merit'&&Object.values(meritSamplingKeys).includes(p[1])){
    state.samplingPlan='custom';$('samplingPlanPreset').value='custom';
    for(const other of document.querySelectorAll('[data-sampling-stage="global"],[data-bind]'))if(other!==input&&(other.dataset.path===JSON.stringify(p)||other.dataset.bind===p.join('.')))other.value=value;
   }
   if(input.closest('[data-paper-workflow]'))renderSearchControls();
   if(p[0]==='merit'&&p[1]==='minimum_relative_illumination')renderMerit();
   scheduleMeritPreview();if(input.tagName==='SELECT'||type==='boolean'||input.dataset.bind==='options.population')render();
  }
 }catch(error){const key=input.dataset.path??(input.dataset.bind?JSON.stringify(path(input.dataset.bind)):input.dataset.meritJson?JSON.stringify(['merit',input.dataset.meritJson]):null);if(key){state.invalid.set(key,{raw:input.value,error:error.message});input.classList.add('json-invalid');input.setCustomValidity(error.message);input.reportValidity();}else{if(input.id==='optimizationScope')input.value=state.optimizationScope;if(input.id==='ordinaryOptimizer')input.value=state.ordinaryMode;if(['initialShapeMode','searchBflReference','searchMaterialScheme'].includes(input.id))renderSearchControls();if(input.id.startsWith('ordinaryDe'))render();if(input.dataset.surfaceVariable)renderSurfaces();notice(error.message,true);}}
});
$('editForm').addEventListener('submit',event=>{event.preventDefault();try{state.dialog.submit(Object.fromEntries(new FormData(event.target)));state.dialog=null;$('editDialog').close();render();}catch(error){notice(error.message,true);}});
$('editDialog').addEventListener('cancel',()=>{state.dialog=null;});
async function initialize(){
 clearPlots();
 const badge=document.createElement('span');badge.id='projectState';badge.className='muted';document.querySelector('.topbar h1').append(badge);
 $('filePath').readOnly=true;$('filePath').placeholder=tr('选择本地项目 JSON 文件','Choose a local project JSON file');
 const apply=document.createElement('button');apply.id='applyResult';apply.className='secondary';apply.dataset.zh='应用结果项目';apply.dataset.en='Apply result project';apply.textContent='应用结果项目';$('refreshJob').after(apply);
 await busy(async()=>{const [b,l]=await Promise.all([api.benchmarks(),api.glassLibraries()]);state.benchmarks=b.benchmarks??b;state.libraries=l.libraries??l;$('benchmarkSelect').innerHTML=state.benchmarks.map(row=>`<option value="${h(row.task??row.id)}" ${(row.task??row.id)==='S3'?'selected':''}>${h(row.task??row.id)}</option>`).join('');accept(await api.benchmark('S3'));await loadGlasses();});
}
Object.defineProperty(window,'lensdeWorkbench',{value:Object.freeze({inspectState:()=>({locale:state.locale,tab:state.tab,project:state.project?scientificSnapshot(state.project):null,job_id:state.jobId,dirty:state.dirty,preset:null})})});
initialize();
