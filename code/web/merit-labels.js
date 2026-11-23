/** Bilingual merit labels retain units and control semantics in both locales.
 * Presentation labels and effective checkbox states never rewrite a project.
 */
import {meritSchema} from './merit-schema.js';

const chinese=Object.fromEntries([...meritSchema.autoKeys,...meritSchema.manualKeys,...meritSchema.unifiedKeys]);
const english={
 target_efl:'Target EFL',target_ttl:'TTL upper limit',target_bfl:'BFL lower limit',
 working_f_number_upper:'Working F/# upper limit (0=no limit)',
 weight_working_f_number:'Working F/# operand weight',
 distortion_upper:'Maximum distortion (0.03=3%)',
 chief_ray_angle_upper:'Maximum chief-ray incidence angle (deg)',
 lateral_color_upper:'Lateral color limit (mm)',
 quality_spot_scale:'Constraint-equivalent RMS spot scale (mm)',
 quality_wavefront_scale:'RMS wavefront normalization scale (waves)',
 unified_image_mean_spot_weight:'Core RMS spot weight',
 unified_image_single_ray_spot_tail_weight:'Absolute single-ray tail weight',
 single_ray_spot_tail_extra_edge_min_field_radius:'Field radius for staggered tail enhancement (>1=off)',
 unified_image_wavefront_weight:'Auxiliary RMS wavefront weight',
 quality_single_ray_guard_ratio:'Single-ray wavefront guard/RMS ratio (legacy spot compatibility)',
 quality_efl_rel_tol:'EFL relative tolerance',quality_ttl_rel_tol:'TTL relative tolerance',
 quality_bfl_rel_tol:'BFL relative tolerance',quality_working_f_number_tol:'Working F/# absolute tolerance',
 quality_length_abs_tol:'Minimum length tolerance (mm)',quality_edge_tol:'Edge-thickness tolerance (mm)',
 quality_ratio_tol:'ATC/ATE/BL0 ratio tolerance',
 minimum_relative_illumination:'Minimum relative illumination (0=off; automatically limits field vignetting)',
 vignetting_boundary_support_weight:'Strict physical-aperture vignetting mismatch weight',
 quality_surface_tangent_angle_tol_deg:'Surface tangent-angle tolerance (deg)',
 quality_distortion_tol:'Distortion tolerance (0.01=1%)',
 quality_chief_ray_angle_tol:'Initial chief-ray angle tolerance (deg)',
 quality_chief_ray_angle_tightened_tol:'Tightened chief-ray angle tolerance (deg)',
 quality_lateral_color_tol:'Lateral color tolerance (mm)',quality_axial_color_tol:'Axial color tolerance (mm)',
 quality_spectral_spot_spread_tol:'Spectral spot-spread tolerance (mm)',min_edge_thickness:'Minimum edge thickness (mm)',
 glass_center_thickness_lower:'Glass center-thickness lower limit (mm)',
 glass_center_thickness_upper:'Glass center-thickness upper limit (mm)',
 air_center_thickness_lower:'Air center-thickness lower limit (mm)',
 air_center_thickness_upper:'Air center-thickness upper limit (mm)',
 glass_edge_thickness_lower:'Glass edge-thickness lower limit (mm)',
 glass_edge_thickness_upper:'Glass edge-thickness upper limit (mm)',
 air_edge_thickness_lower:'Air edge-thickness lower limit (mm)',
 air_edge_thickness_upper:'Air edge-thickness upper limit (mm)',
 structural_prior_tight_triplet_weight:'Same-sign tight-triplet soft-prior weight (0=off)',
 structural_prior_gap_tolerance:'Tight air-gap transition width (mm)',
 pupil_order_prior_weight:'Meridional ray-order soft-prior weight (0=off)',
 pupil_order_jacobian_floor:'Meridional ray-order soft Jacobian floor',
 pupil_order_soft_tolerance:'Meridional ray-order soft transition width',
 pupil_order_hard_jacobian:'Severe ray-folding hard threshold',
 pupil_order_hard_max_progress:'Maximum group progress for severe-folding checks',
 pupil_order_hard_consecutive_surfaces:'Consecutive surfaces for severe-folding checks',
 pupil_order_min_field_radius:'Minimum field radius for ray-order checks',
 atc_lower:'ATC lower limit',atc_upper:'ATC upper limit (0=no limit)',
 ate_lower:'ATE lower limit',ate_upper:'ATE upper limit (0=no limit)',
 bl0_lower:'BL0 lower limit (0=no limit)',bl0_upper:'BL0 upper limit (0=no limit)',
 surface_tangent_angle_lower_deg:'Minimum surface tangent/axis angle (deg; 0=no limit)',
 surface_tangent_angle_samples:'Radial samples for surface tangent angles',fail_merit:'Failure merit',
 weight_edge:'Edge-thickness weight',group_weight_constraint:'Manufacturing constraint-equivalent RMS factor',
 group_weight_first_order:'First-order group weight',group_weight_image:'Image-quality group weight',
 unified_first_order_working_f_number_weight:'Working F/# first-order group weight',
 unified_loss_delta:'Pseudo-Huber delta',unified_min_first_order_gate:'Minimum first-order gate',
};
const extra={
 auto_merit:['自动评价函数','Automatic merit function'],merit_model:['评价模型','Merit model'],
 chromatic_spot_merit_enabled:['色差辅助监督','Auxiliary chromatic supervision'],
 vignetting_physical_cut_match_enabled:['物理孔径渐晕残差','Physical-aperture vignetting matching residual'],
 distortion_model:['畸变模型','Distortion model'],image_quality_metric:['像质指标','Image-quality metric'],
 wavefront_reference:['波前参考球','Wavefront reference sphere'],
};
export const meritLabelKeys=Object.freeze([...new Set([...Object.keys(chinese),...Object.keys(extra)])]);
export function meritLabel(key,locale='zh-CN'){
 if(Object.hasOwn(extra,key))return extra[key][locale==='zh-CN'?0:1];
 return (locale==='zh-CN'?chinese:english)[key]??key;
}

const enumSources={merit_model:'meritModelOptions',distortion_model:'distortionModelOptions',
 image_quality_metric:'imageQualityMetricOptions',wavefront_reference:'wavefrontReferenceOptions'};
const enumEnglish={auto:'Automatic',f_tan_theta:'F–tan θ',f_theta:'F–θ',rms_spot:'RMS spot',
 rms_wavefront:'RMS wavefront',rms_spot_wavefront:'RMS spot + RMS wavefront',
 chief_ray:'Chief-ray reference sphere',centroid_sphere:'Centroid reference sphere'};
export function meritChoiceLabel(key,value,locale='zh-CN'){
 const row=meritSchema[enumSources[key]]?.find(row=>row[0]===value);
 return locale==='zh-CN'?(row?.[1]??value):(enumEnglish[value]??value);
}

export function vignettingControlState(project){
 const merit=project.merit??{},raw=Number(merit.minimum_relative_illumination??merit.vignetting_minimum_relative_illumination??0);
 const floor=Number.isFinite(raw)?Math.min(1,Math.max(0,raw)):0;
 const requested=merit.vignetting_physical_cut_match_enabled??merit.vignetting_global_trim_cut_match_experimental??true;
 const enabled=floor>0;
 return {floor,enabled,physicalMatchRequested:requested===true,physicalMatchEffective:enabled&&requested===true};
}
