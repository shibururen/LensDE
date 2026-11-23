/* Labels/enums from the original workbench; no scientific defaults. */
export const meritSchema = {
  "autoKeys": [
    [
      "target_efl",
      "目标 EFL"
    ],
    [
      "target_ttl",
      "TTL 上限"
    ],
    [
      "target_bfl",
      "BFL 下限"
    ],
    [
      "working_f_number_upper",
      "实际工作 F/# 上限 (0=无)"
    ],
    [
      "weight_working_f_number",
      "实际工作 F/# 操作数权重"
    ],
    [
      "distortion_upper",
      "最大畸变上限 (0.03=3%)"
    ],
    [
      "chief_ray_angle_upper",
      "最大主光线入射角上限 deg"
    ],
    [
      "lateral_color_upper",
      "垂轴色差上限 mm"
    ],
    [
      "quality_spot_scale",
      "约束等效RMS spot尺度 mm"
    ],
    [
      "quality_wavefront_scale",
      "波前RMS归一化尺度 waves"
    ],
    [
      "unified_image_mean_spot_weight",
      "RMS spot核心权重"
    ],
    [
      "unified_image_single_ray_spot_tail_weight",
      "单光线绝对尾项权重"
    ],
    [
      "single_ray_spot_tail_extra_edge_min_field_radius",
      "尾项交错边界增强起始视场半径 (>1=关闭)"
    ],
    [
      "unified_image_wavefront_weight",
      "RMS wavefront辅助权重"
    ],
    [
      "quality_single_ray_guard_ratio",
      "单光线波前guard/RMS倍率（spot旧工程兼容）"
    ],
    [
      "quality_efl_rel_tol",
      "EFL 相对容差"
    ],
    [
      "quality_ttl_rel_tol",
      "TTL 相对容差"
    ],
    [
      "quality_bfl_rel_tol",
      "BFL 相对容差"
    ],
    [
      "quality_working_f_number_tol",
      "实际工作 F/# 绝对容差"
    ],
    [
      "quality_length_abs_tol",
      "长度最小容差 mm"
    ],
    [
      "quality_edge_tol",
      "边厚容差 mm"
    ],
    [
      "quality_ratio_tol",
      "ATC/ATE/BL0比例容差"
    ],
    [
      "minimum_relative_illumination",
      "最小相对照度（0=关闭；自动限制各视场渐晕）"
    ],
    [
      "vignetting_boundary_support_weight",
      "严格物理孔径渐晕偏差权重"
    ],
    [
      "quality_surface_tangent_angle_tol_deg",
      "表面切线角容差 deg"
    ],
    [
      "quality_distortion_tol",
      "畸变容差 (0.01=1%)"
    ],
    [
      "quality_chief_ray_angle_tol",
      "主光线角起始容差 deg"
    ],
    [
      "quality_chief_ray_angle_tightened_tol",
      "主光线角收紧容差 deg"
    ],
    [
      "quality_lateral_color_tol",
      "垂轴色差容差 mm"
    ],
    [
      "quality_axial_color_tol",
      "轴向色差容差 mm"
    ],
    [
      "quality_spectral_spot_spread_tol",
      "光谱光斑离散容差 mm"
    ],
    [
      "min_edge_thickness",
      "最小边厚 mm"
    ],
    [
      "glass_center_thickness_lower",
      "玻璃中心厚下限 mm"
    ],
    [
      "glass_center_thickness_upper",
      "玻璃中心厚上限 mm"
    ],
    [
      "air_center_thickness_lower",
      "空气中心厚下限 mm"
    ],
    [
      "air_center_thickness_upper",
      "空气中心厚上限 mm"
    ],
    [
      "glass_edge_thickness_lower",
      "玻璃边厚下限 mm"
    ],
    [
      "glass_edge_thickness_upper",
      "玻璃边厚上限 mm"
    ],
    [
      "air_edge_thickness_lower",
      "空气边厚下限 mm"
    ],
    [
      "air_edge_thickness_upper",
      "空气边厚上限 mm"
    ],
    [
      "structural_prior_tight_triplet_weight",
      "三片同号紧贴结构软先验权重（0=关闭）"
    ],
    [
      "structural_prior_gap_tolerance",
      "紧贴空气间隔过渡宽度 mm"
    ],
    [
      "pupil_order_prior_weight",
      "子午光线次序软先验权重（0=关闭）"
    ],
    [
      "pupil_order_jacobian_floor",
      "子午光线次序雅可比软下限"
    ],
    [
      "pupil_order_soft_tolerance",
      "子午光线次序软过渡宽度"
    ],
    [
      "pupil_order_hard_jacobian",
      "子午光线严重折叠硬阈值"
    ],
    [
      "pupil_order_hard_max_progress",
      "严重折叠检查最大镜组进度"
    ],
    [
      "pupil_order_hard_consecutive_surfaces",
      "严重折叠连续表面数"
    ],
    [
      "pupil_order_min_field_radius",
      "子午光线次序检查最小视场"
    ],
    [
      "atc_lower",
      "ATC下限"
    ],
    [
      "atc_upper",
      "ATC上限 (0=无)"
    ],
    [
      "ate_lower",
      "ATE下限"
    ],
    [
      "ate_upper",
      "ATE上限 (0=无)"
    ],
    [
      "bl0_lower",
      "BL0下限 (0=无)"
    ],
    [
      "bl0_upper",
      "BL0上限 (0=无)"
    ],
    [
      "surface_tangent_angle_lower_deg",
      "表面切线-光轴夹角下限 deg (0=无)"
    ],
    [
      "surface_tangent_angle_samples",
      "表面切线角径向采样点"
    ],
    [
      "fail_merit",
      "失效 merit"
    ]
  ],
  "manualKeys": [
    [
      "min_edge_thickness",
      "最小边厚 mm"
    ],
    [
      "glass_center_thickness_lower",
      "玻璃中心厚下限 mm"
    ],
    [
      "glass_center_thickness_upper",
      "玻璃中心厚上限 mm"
    ],
    [
      "air_center_thickness_lower",
      "空气中心厚下限 mm"
    ],
    [
      "air_center_thickness_upper",
      "空气中心厚上限 mm"
    ],
    [
      "glass_edge_thickness_lower",
      "玻璃边厚下限 mm"
    ],
    [
      "glass_edge_thickness_upper",
      "玻璃边厚上限 mm"
    ],
    [
      "air_edge_thickness_lower",
      "空气边厚下限 mm"
    ],
    [
      "air_edge_thickness_upper",
      "空气边厚上限 mm"
    ],
    [
      "structural_prior_tight_triplet_weight",
      "三片同号紧贴结构软先验权重（0=关闭）"
    ],
    [
      "structural_prior_gap_tolerance",
      "紧贴空气间隔过渡宽度 mm"
    ],
    [
      "pupil_order_prior_weight",
      "子午光线次序软先验权重（0=关闭）"
    ],
    [
      "pupil_order_jacobian_floor",
      "子午光线次序雅可比软下限"
    ],
    [
      "pupil_order_soft_tolerance",
      "子午光线次序软过渡宽度"
    ],
    [
      "pupil_order_hard_jacobian",
      "子午光线严重折叠硬阈值"
    ],
    [
      "pupil_order_hard_max_progress",
      "严重折叠检查最大镜组进度"
    ],
    [
      "pupil_order_hard_consecutive_surfaces",
      "严重折叠连续表面数"
    ],
    [
      "pupil_order_min_field_radius",
      "子午光线次序检查最小视场"
    ],
    [
      "atc_lower",
      "ATC下限"
    ],
    [
      "atc_upper",
      "ATC上限 (0=无)"
    ],
    [
      "ate_lower",
      "ATE下限"
    ],
    [
      "ate_upper",
      "ATE上限 (0=无)"
    ],
    [
      "bl0_lower",
      "BL0下限 (0=无)"
    ],
    [
      "bl0_upper",
      "BL0上限 (0=无)"
    ],
    [
      "surface_tangent_angle_lower_deg",
      "表面切线-光轴夹角下限 deg (0=无)"
    ],
    [
      "surface_tangent_angle_samples",
      "表面切线角径向采样点"
    ],
    [
      "quality_surface_tangent_angle_tol_deg",
      "表面切线角容差 deg"
    ],
    [
      "minimum_relative_illumination",
      "最小相对照度（0=关闭；自动限制各视场渐晕）"
    ],
    [
      "vignetting_boundary_support_weight",
      "严格物理孔径渐晕偏差权重"
    ],
    [
      "weight_edge",
      "边厚权重"
    ],
    [
      "fail_merit",
      "失效 merit"
    ]
  ],
  "unifiedKeys": [
    [
      "group_weight_constraint",
      "制造约束等效RMS倍率"
    ],
    [
      "group_weight_first_order",
      "一阶组权重"
    ],
    [
      "group_weight_image",
      "像质组权重"
    ],
    [
      "unified_first_order_working_f_number_weight",
      "实际工作 F/# 一阶组权重"
    ],
    [
      "unified_loss_delta",
      "Pseudo-Huber delta"
    ],
    [
      "unified_min_first_order_gate",
      "一阶最小gate"
    ]
  ],
  "meritModelOptions": [
    [
      "unified_trace_gate",
      "unified_trace_gate"
    ],
    [
      "adaptive_quality",
      "adaptive_quality"
    ],
    [
      "coverage_first",
      "coverage_first"
    ],
    [
      "weighted_rms",
      "weighted_rms"
    ],
    [
      "trace_guarded_adaptive",
      "trace_guarded_adaptive"
    ],
    [
      "unified_adaptive",
      "unified_adaptive"
    ],
    [
      "trace_guarded",
      "trace_guarded"
    ]
  ],
  "distortionModelOptions": [
    [
      "auto",
      "自动"
    ],
    [
      "f_tan_theta",
      "F-TAN THETA"
    ],
    [
      "f_theta",
      "F-THETA"
    ]
  ],
  "imageQualityMetricOptions": [
    [
      "rms_spot",
      "RMS spot"
    ],
    [
      "rms_wavefront",
      "RMS 波前"
    ],
    [
      "rms_spot_wavefront",
      "RMS spot + RMS 波前"
    ]
  ],
  "wavefrontReferenceOptions": [
    [
      "chief_ray",
      "主光线参考球"
    ],
    [
      "centroid_sphere",
      "质心参考球"
    ]
  ]
};
