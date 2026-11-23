#pragma once
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstring>
namespace optiland {
struct EvaluationCertificate {
    int eligible = 0;
    int field = -1;
    int reason = 0; // 1/2: trace proof, 3: conditional RMS-image Merit proof
    int parent_trace_feasible = 0;
    double parent_trace_violation = 0.0;
    double coverage_upper = 1.0;
    double failed_weight_lower = 0.0;
    double total_weight_upper = 0.0;
    double trace_violation_lower = 0.0;
    int merit_eligible = 0;
    int prefix_fields = 0;
    double parent_adjusted_merit = 0.0;
    double merit_lower = 0.0;
    double image_denominator_upper = 0.0;
    double image_gate_lower = 0.0;
    double rms_sum_lower = 0.0;
};
void evaluation_set_certificate_context(int mode, const std::vector<EvaluationCertificate>& rows, double epsilon);
std::vector<EvaluationCertificate> evaluation_certificate_results();
struct EvaluationCallerContext {
    int mode = 0;
    double epsilon = 0.0;
    std::vector<EvaluationCertificate> rows;
};
// The owning translation unit provides one context per thread to all callers.
EvaluationCallerContext& evaluation_caller_context_storage();
inline void evaluation_local_set_context(int mode, const std::vector<EvaluationCertificate>& rows, double epsilon) {
    auto& caller_context = evaluation_caller_context_storage();
    caller_context = EvaluationCallerContext{};
    if (mode) { caller_context.mode=mode; caller_context.epsilon=epsilon; caller_context.rows=rows; }
}
inline EvaluationCallerContext evaluation_local_context() { return evaluation_caller_context_storage(); }
inline std::vector<EvaluationCertificate> evaluation_local_results() { return evaluation_caller_context_storage().rows; }
inline void evaluation_local_complete(const EvaluationCallerContext& request, const std::vector<EvaluationCertificate>& result) {
    evaluation_local_set_context(request.mode,result,request.epsilon);
}
inline bool evaluation_context_compatible(const EvaluationCallerContext& a, const EvaluationCallerContext& b) {
    return !a.mode || !b.mode || (a.mode==b.mode && a.epsilon==b.epsilon);
}
inline bool evaluation_parent_identity_equal(const EvaluationCertificate& a, const EvaluationCertificate& b) {
    const auto same=[](double x,double y){return x==y || (std::isnan(x)&&std::isnan(y));};
    return a.eligible==b.eligible && a.parent_trace_feasible==b.parent_trace_feasible &&
        same(a.parent_trace_violation,b.parent_trace_violation) && a.merit_eligible==b.merit_eligible &&
        same(a.parent_adjusted_merit,b.parent_adjusted_merit);
}
inline bool evaluation_request_valid(const EvaluationCallerContext& context,int rows,bool summaries) {
    if (!context.mode) return context.rows.empty();
    return (context.mode==1 || context.mode==2) && summaries && rows>0 &&
        context.rows.size()==static_cast<std::size_t>(rows) && std::isfinite(context.epsilon) && context.epsilon>=0.0;
}

// 0: full score, 1: proof-only shadow, 2: consume proven rejection. Unknown
// values disable pruning; a missing setting enables the validated trace path.
inline int evaluation_pruning_mode() {
    const char* value=std::getenv("OPTILAND_DE_CUDA_EVALUATION_PRUNING");
    if(!value || std::strcmp(value,"trace")==0 || std::strcmp(value,"2")==0)return 2;
    if(std::strcmp(value,"shadow")==0 || std::strcmp(value,"1")==0)return 1;
    return 0;
}
inline bool evaluation_diagnostics_safe() {
    for(const char* key:{"OPTILAND_TRIAL_AUDIT_BUILDERS","OPTILAND_BUILDER_DIAGNOSTIC_CSV",
        "OPTILAND_RESEARCH_FIELD_PREFIX","OPTILAND_RESEARCH_FIELD_PREFIX_JSONL"})
        if(std::getenv(key))return false; // Legacy observers use presence, even "0".
    return true;
}
struct EvaluationLocalReset {
    EvaluationLocalReset() = default;
    EvaluationLocalReset(const EvaluationLocalReset&) = delete;
    EvaluationLocalReset& operator=(const EvaluationLocalReset&) = delete;
    ~EvaluationLocalReset() { evaluation_local_set_context(0,{},0.0); }
};
}
