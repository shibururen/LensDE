import {assertProject,clone,syncExistingVariableInitials,optimizationSelection} from './model.js';
import {captureSampling} from './sampling-plans.js';
export const state={locale:localStorage.getItem('lensde.locale')==='en'?'en':'zh-CN',project:null,tab:'surfaces',optimizationScope:'ordinary',ordinaryMode:'de',busy:false,dirty:false,
  benchmarks:[],libraries:[],materials:[],selectedSurface:1,selectedVariable:0,selectedField:0,jobId:'',job:null,poll:null,
  invalid:new Map(),meritPreview:null,sourceRevision:0,jobRevision:null,appliedResultJob:null,jobProject:null,dialog:null,samplingBaseline:null,samplingPlan:'task_default'};
export const $=id=>document.getElementById(id);
export const tr=(zh,en)=>state.locale==='zh-CN'?zh:en;
export const words=key=>String(key).replace(/_/g,' ').replace(/\b(efl|ttl|bfl|dls|de|rms|cpu|cuda|cra|nd|vd|atc|ate)\b/gi,s=>s.toUpperCase());
export const path=raw=>raw.split('.').map(k=>/^\d+$/.test(k)?Number(k):k);
export function guard(){if(state.invalid.size)throw Error(tr('请先修正无效输入。','Correct invalid inputs first.'));if(state.dialog)throw Error(tr('请先应用或取消当前编辑。','Apply or cancel the current edit first.'));}
export function modified(project,{sync=false}={}){state.project=sync?syncExistingVariableInitials(project):project;state.dirty=true;state.sourceRevision++;state.meritPreview=null;if($('projectState'))$('projectState').textContent=tr('已修改','Modified');}
export function accept(response){state.project=clone(assertProject(response.project??response));const selection=optimizationSelection(state.project);state.optimizationScope=selection.scope;state.ordinaryMode=selection.ordinaryMode;state.samplingBaseline=captureSampling(state.project);state.samplingPlan='task_default';state.invalid.clear();state.dirty=false;state.sourceRevision++;state.meritPreview=null;}
export function notice(message,error=false){$('notice').hidden=false;$('notice').textContent=message;$('notice').className=error?'error':'';}
