import {state,$,tr} from './workbench-state.js';
const pairs={
 '表面编辑、变量配置、追迹分析与优化调用':'Surface prescription, variables, ray tracing and native optimization',
 '文件':'Files','系统':'System','光线瞄准':'Ray aiming','优化':'Optimization','运行追迹':'Run trace','运行优化':'Run optimization','初始结构搜索':'Initial structure search','新建':'New from S3',
 '路径':'Path','加载工程':'Open project','保存工程':'Save project','导入 Optiland':'Import system JSON','导出系统':'Export system','导出 Code V':'Export Code V','导出 Zemax':'Export Zemax',
 '玻璃搜索库':'Glass search libraries','模式':'Mode','初始结构':'Initial structure','初始 DE 非球面':'Initial DE aspheres','制造约束初始化':'Manufacturing constrained initialization',
 '表面':'Surfaces','变量':'Variables','视场/波长':'Fields / wavelengths','优化函数':'Merit function','分析/输出':'Analysis / output','表面定义':'Surface prescription','变量配置':'Variables',
 '添加表面':'Add surface','复制当前':'Duplicate selected','删除当前':'Delete selected','添加变量':'Add variable','删除变量':'Delete variable','视场':'Fields','波长':'Wavelengths',
 '设置渐晕':'Set vignetting','移除渐晕':'Clear vignetting','添加视场':'Add field','删除视场':'Delete field','添加波长':'Add wavelength','打开 SVG':'Open SVG','点列图 RMS':'RMS spot diagram','运行追迹后显示':'Run a trace to display results','评分':'Score','日志':'Log',
 '广角前组先验':'Wide angle front group prior','通用边界、DE 参数 / 旧版单路流程参数':'Bounds and DE settings','方向 Scout 与种群设置':'Directional scout and population',
 '分阶段非球面（可选）':'Staged aspheres (optional)','Material 后置路线':'Material stage route','Material Funnel 前色差材料修复':'Chromatic repair before material funnel','Material Bridge / 孔径冲击修复':'Material bridge / aperture repair',
 '连续材料惩罚 / 连通包络':'Continuous material penalty / connected envelope','初始 Merit':'Initial merit','初始搜索 Calibrated':'Initial calibrated aiming','初始 DLS':'Initial DLS',
 'Run refinement':'Run refinement',
};
const originalTextNodes=new WeakMap();
export function localize(){
 document.documentElement.lang=state.locale;document.title='LensDE Workbench';
 for(const el of document.querySelectorAll('[data-zh]'))el.textContent=state.locale==='zh-CN'?el.dataset.zh:el.dataset.en;
 for(const el of document.querySelectorAll('h2,h3,h4,button,a.link-button,.topbar p,.tabs button')){
  if(el.dataset.zh)continue;if(!el.dataset.originalText)el.dataset.originalText=el.textContent.trim();
  el.textContent=state.locale==='zh-CN'?el.dataset.originalText:(pairs[el.dataset.originalText]??el.dataset.originalText);
 }
 $('localeZh').textContent='中文';$('localeEn').textContent='EN';$('newProject').textContent=tr('新建（S3参考）','New from S3');
 for(const label of document.querySelectorAll('label'))for(const node of label.childNodes){if(node.nodeType!==Node.TEXT_NODE)continue;const original=originalTextNodes.get(node)??node.textContent.trim();if(!pairs[original])continue;originalTextNodes.set(node,original);node.textContent=(state.locale==='zh-CN'?original:pairs[original])+' ';}
 $('filePath').placeholder=tr('选择本地项目 JSON 文件','Choose a local project JSON file');
 $('importOptiland').textContent=tr('导入系统 JSON','Import system JSON');
 if($('projectState'))$('projectState').textContent=state.dirty?tr('已修改','Modified'):tr('已装载','Loaded');
 $('localeZh').classList.toggle('active',state.locale==='zh-CN');$('localeEn').classList.toggle('active',state.locale==='en');
}
