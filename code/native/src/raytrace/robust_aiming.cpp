// Implement the retained centered sequential adaptation of Optiland robust pupil aiming.
// AimingSession owns the robust state and exact-key reuse; upstream attribution follows below.
// Keep transverse-coordinate solves and clipping-free aiming distinct from physical trace clipping.

// Algorithm adapted from Optiland robust.py, iterative.py, parameterization.py
// and pupil_map.py at af111f1655fd5f2e253201d59f431ad1757318c4.
// Copyright (c) 2024 Kramer Harrison. MIT license: see
// ../../../third_party/optiland_LICENSE.txt.
// Centered sequential CPU adaptation: scalar solves, per-session exact-key
// cache and beam-transverse coordinates. Aiming ignores aperture clipping;
// the ordinary physical trace continues to enforce the physical aperture.
#include "diagnostics_inline.hpp"
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

class RobustAimingState {
    using XY = std::array<double, 2>;
    using Matrix = std::array<double, 4>;
    using Key = std::tuple<double, double, double>;
    struct Bound {
        Ray seed;
        bool finite;
        Vec3 u, v;
        Bound(const Ray& ray, bool finite_object) : seed(ray), finite(finite_object) {
            const Vec3 k = normalize(seed.d);
            u = Vec3{1, 0, 0} - k * k.x;
            if (norm(u) < std::sqrt(std::numeric_limits<double>::epsilon()))
                u = Vec3{0, 1, 0} - k * k.y;
            u = normalize(u);
            v = {k.y*u.z-k.z*u.y, k.z*u.x-k.x*u.z, k.x*u.y-k.y*u.x};
            if(!finite) {
                // A sagittal/meridional frame makes the scan and field carry
                // covariant under axial rotation of a centered prescription.
                const double radial=std::hypot(k.x,k.y);
                u=radial>1e-14?Vec3{k.y/radial,-k.x/radial,0}:Vec3{1,0,0};
                v={k.y*u.z-k.z*u.y,k.z*u.x-k.x*u.z,k.x*u.y-k.y*u.x};
            }
#ifdef OPTILAND_ROBUST_UPSTREAM_ENTRY_FRAME
            // Audit variant: the literal canonical upstream position basis.
            if (!finite) { u = {1,0,0}; v = {0,1,0}; }
#endif
        }
        Ray launch(XY q) const {
            Ray r = seed;
            r.valid = true; r.intensity = 1.0; r.opd = 0.0;
            r.failure_code = RayFailureCode::None; r.failure_surface = -1;
            r.stop_reached = false;
            if (finite) r.d = normalize(seed.d + u*q[0] + v*q[1]);
            else r.p = seed.p + u*q[0] + v*q[1];
            return r;
        }
        XY project(const Ray& r) const {
            if (!finite) return {dot(r.p-seed.p,u), dot(r.p-seed.p,v)};
            const double along = dot(r.d,normalize(seed.d));
            if (along <= 1e-14) return {kInf,kInf};
            return {dot(r.d,u)/along, dot(r.d,v)/along};
        }
    };
    struct Map {
        Field field;
        Ray chief;
        XY offset{0,0};
        Matrix affine{0,0,0,0};
        std::string strategy = "failed";
        bool valid = false;
    };
    const LensSystem& system;
    std::unique_ptr<SequentialRayTracer> tracer;
    std::vector<double> fingerprint;
    std::map<Key,Map> maps;
    std::map<double,double> paraxial_responses;
    bool finite() const { return std::isfinite(system.object_distance()); }
    static bool usable(XY a) { return std::isfinite(a[0]) && std::isfinite(a[1]); }
    static double length(XY a) { return std::hypot(a[0],a[1]); }
    static XY difference(XY a, XY b) { return {a[0]-b[0],a[1]-b[1]}; }
    static double ms(long long start) {
        return (aiming_clock_ns()-start)*1e-6;
    }
    void refresh() {
        // Compare optical values, not addresses or caller-supplied field indices.
        // Whole-system coverage also invalidates paraxial EPL/EPD cached by tracer.
        std::vector<double> f;
        auto add = [&](double x) { f.push_back(std::isnan(x) ? -kInf : x); };
        auto array = [&](const std::vector<double>& a) { add(double(a.size())); for(double x:a)add(x); };
        add(system.max_field_deg); add(system.aperture.value); add(int(system.aperture.type));
        add(system.primary_wavelength()); add(system.primary_wavelength_apertures);
        add(system.ray_aiming.tolerance); add(system.ray_aiming.max_iter);
        add(system.ray_aiming.start_offset); add(int(system.ray_aiming.stop_radius_source));
        add(int(asphere_root_policy()));
        add(double(system.surfaces.size()));
        for(const auto& s:system.surfaces) {
            add(int(s.kind)); add(s.radius); add(s.thickness); add(s.conic);
            array(s.asphere_coefficients); add(s.newton_tol); add(s.newton_max_iter);
            add(s.semi_aperture); add(s.mechanical_aperture); add(s.aperture_radius);
            add(s.clear_aperture_fixed); add(s.mechanical_aperture_fixed);
            add(s.is_stop); add(s.reflective);
            const auto& m=s.material_after;
            add(int(m.model)); add(m.nd); add(m.vd); add(m.formula_type);
            array(m.coefficients); array(m.table_wavelengths); array(m.table_n);
        }
        if (!tracer || f != fingerprint) {
            if(tracer) ++report.invalidations;
            maps.clear(); paraxial_responses.clear(); fingerprint=std::move(f);
            tracer=std::make_unique<SequentialRayTracer>(system);
        }
    }
    Ray paraxial(Field f, double wave) const {
        // The calibration chief is the STOP center. FVC/FVD are applied once,
        // to requested pupil targets, including the production reference chief.
        f.fvcx=f.fvcy=f.fvdx=f.fvdy=0;
        return tracer->generate_ray_with_mode(f,{0,0,1},wave,RayAimingMode::Paraxial);
    }
    bool stop(const Ray& r, XY& hit) {
        ++report.stop_traces;
        TracePoint p;
        const bool ok=tracer->trace_to_surface(r,tracer->stop_surface_index(),&p,true,true);
        hit = ok && p.valid ? XY{p.x,p.y} : XY{kInf,kInf};
        return ok && p.valid && usable(hit);
    }
    double paraxial_response(const Ray& seed) {
        const auto found=paraxial_responses.find(seed.wavelength_um);
        if(found!=paraxial_responses.end()) return found->second;
        // Signed first-order response, evaluated by the same geometry tracer
        // on axis. Finite conjugates use tangent-direction parameters.
        Field axis;
        Bound b(paraxial(axis,seed.wavelength_um),finite());
        XY plus,minus;
        const double h=1e-5;
        if(!stop(b.launch({h,0}),plus)||!stop(b.launch({-h,0}),minus)) return 0;
        const double value=(plus[0]-minus[0])/(2*h);
        paraxial_responses.emplace(seed.wavelength_um,value);
        return value;
    }
    Matrix jacobian(const Bound& b, XY q, double scale, double fallback) {
        const double h0=std::cbrt(std::numeric_limits<double>::epsilon());
        const double hx=h0*std::max(scale,std::abs(q[0]));
        const double hy=h0*std::max(scale,std::abs(q[1]));
        XY xp,xm,yp,ym;
        const bool a=stop(b.launch({q[0]+hx,q[1]}),xp);
        const bool c=stop(b.launch({q[0]-hx,q[1]}),xm);
        const bool d=stop(b.launch({q[0],q[1]+hy}),yp);
        const bool e=stop(b.launch({q[0],q[1]-hy}),ym);
        if(a&&c&&d&&e) return {(xp[0]-xm[0])/(2*hx),(yp[0]-ym[0])/(2*hy),
                              (xp[1]-xm[1])/(2*hx),(yp[1]-ym[1])/(2*hy)};
        ++report.jacobian_fallbacks;
        return {fallback,0,0,fallback};
    }
    static bool step(Matrix j, XY error, XY& delta) {
        const double scale=std::max({std::abs(j[0]),std::abs(j[1]),std::abs(j[2]),std::abs(j[3])});
        if(!std::isfinite(scale)||scale==0) return false;
        for(double& x:j)x/=scale;
        const double det=j[0]*j[3]-j[1]*j[2];
        const double n=std::max(std::abs(j[0])+std::abs(j[1]),std::abs(j[2])+std::abs(j[3]));
        const double adj=std::max(std::abs(j[3])+std::abs(j[1]),std::abs(j[2])+std::abs(j[0]));
        if(std::abs(det)<=64*std::numeric_limits<double>::epsilon()*n*adj) return false;
        delta={(j[3]*error[0]-j[1]*error[1])/(det*scale),
               (j[0]*error[1]-j[2]*error[0])/(det*scale)};
        return usable(delta);
    }
    Ray solve(const Ray& seed, XY target, double tolerance) {
        ++report.solves;
        Bound b(seed,finite()); XY q{0,0},hit,error{kInf,kInf};
        Ray r=b.launch(q); int iterations=0;
        if(stop(r,hit)) error=difference(hit,target);
        Matrix j{0,0,0,0};
        double fallback=0;
        const double scale=finite()?1:std::max({1.,std::abs(target[0]),std::abs(target[1])});
        if(usable(error) && length(error)>=tolerance) {
            fallback=paraxial_response(seed);
            j=jacobian(b,q,scale,fallback);
        }
        for(int i=0;i<std::max(0,system.ray_aiming.max_iter)&&usable(error)&&length(error)>=tolerance;++i) {
            iterations=i+1;
            XY delta;
            if(!step(j,error,delta)) {
                ++report.jacobian_refreshes;
                j=jacobian(b,q,scale,fallback);
                if(!step(j,error,delta)) {
                    ++report.jacobian_fallbacks; j={fallback,0,0,fallback};
                    if(!step(j,error,delta)) break;
                }
            }
            XY next=q,new_error=error; bool accepted=false;
            double damping=1;
            for(int bt=0;bt<8;++bt,damping*=.5) {
                next={q[0]-damping*delta[0],q[1]-damping*delta[1]};
                if(stop(b.launch(next),hit)) {
                    new_error=difference(hit,target);
                    if(length(new_error)<length(error)) {accepted=true;break;}
                }
            }
            if(!accepted) break;
            const XY s=difference(next,q), y=difference(new_error,error);
            const double ss=s[0]*s[0]+s[1]*s[1];
            const double minimum_broyden_step=std::numeric_limits<double>::epsilon()*std::max({scale,std::abs(q[0]),std::abs(q[1])});
            if(ss>minimum_broyden_step*minimum_broyden_step) {
                const XY correction{y[0]-j[0]*s[0]-j[1]*s[1],y[1]-j[2]*s[0]-j[3]*s[1]};
                j[0]+=correction[0]*s[0]/ss; j[1]+=correction[0]*s[1]/ss;
                j[2]+=correction[1]*s[0]/ss; j[3]+=correction[1]*s[1]/ss;
            }
            q=next; error=new_error;
        }
        r=b.launch(q);
        r.aim_residual=length(error); r.aim_iterations=iterations;
        r.aim_converged=usable(error)&&r.aim_residual<tolerance;
        r.homotopy_fraction=1;
        // Do not turn a finite launch or an intermediate-field success into an
        // accepted requested ray when exact STOP convergence failed.
        if(!r.aim_converged) { r.valid=false; r.intensity=0; r.failure_code=RayFailureCode::AimNotConverged; }
        return r;
    }
    Ray carry(const Ray& fresh, const Ray& previous, XY offset) const {
        Bound b(fresh,finite());
        return b.launch(finite()?offset:b.project(previous));
    }
    Map calibrate(const Field& field,double wave,double radius) {
        ++report.calibrations;
        const auto chief_start=aiming_clock_ns();
        Map result; result.field=field;
        const Ray fresh=paraxial(field,wave);
        const double tol=system.ray_aiming.tolerance;
        if(!fresh.valid || !std::isfinite(norm(fresh.p)) || !std::isfinite(norm(fresh.d))) {
            result.chief=fresh; result.chief.valid=false; result.chief.aim_converged=false;
            result.chief.aim_residual=kInf; result.chief.intensity=0;
            result.chief.failure_code=RayFailureCode::AimNotConverged;
            report.chief_ms+=ms(chief_start);
            return result;
        }
        Ray chief=fresh; chief.aim_converged=false;
        const Map* nearest=nullptr; double distance=kInf;
        for(const auto& kv:maps) if(kv.second.valid) {
            const double h=std::hypot(field.hx-kv.second.field.hx,field.hy-kv.second.field.hy);
            const double d=h+std::abs(wave-kv.second.chief.wavelength_um);
            if(d<distance) {distance=d;nearest=&kv.second;}
        }
        if(nearest) {
            chief=solve(carry(fresh,nearest->chief,nearest->offset),{0,0},tol);
            if(chief.aim_converged) result.strategy="warm_map";
        }
        if(!chief.aim_converged) {
            chief=solve(fresh,{0,0},tol);
            if(chief.aim_converged) result.strategy="direct_paraxial";
        }
        if(!chief.aim_converged) {
            Field axis; Ray good=solve(paraxial(axis,wave),{0,0},tol);
            XY offset{0,0}; double t=0,dt=finite()?.5:1;
            for(int attempt=0;good.aim_converged&&attempt<150&&t<1;++attempt) {
                ++report.march_attempts;
                const double next=std::min(1.,t+dt);
                Field f=field; f.hx*=next; f.hy*=next;
                Ray seed=paraxial(f,wave);
                Ray trial=solve(carry(seed,good,offset),{0,0},std::max(tol,1e-4));
                if(!trial.aim_converged) trial=solve(seed,{0,0},std::max(tol,1e-4));
                if(trial.aim_converged) {
                    good=trial; t=next; offset=Bound(seed,finite()).project(good);
                } else {dt*=.5;if(dt<1e-4)break;}
            }
            if(t>=1) {
                chief=solve(good,{0,0},tol);
                if(chief.aim_converged) result.strategy="marching";
            }
        }
        if(!chief.aim_converged) {
            // Same 2001-point line and nearest-seed root preference as upstream;
            // sorted scalar evaluation can stop at the first converged candidate.
            Bound b(fresh,finite());
            const double h=std::hypot(field.hx,field.hy);
            double dx=h<1e-12?0:field.hx/h,dy=h<1e-12?1:field.hy/h;
#ifndef OPTILAND_ROBUST_UPSTREAM_ENTRY_FRAME
            if(!finite()) { dx=0; dy=1; }
#endif
            Vec3 crossing=fresh.p;
            if(finite()&&std::abs(fresh.d.z)>1e-14)
                crossing=crossing+fresh.d*(-fresh.p.z/fresh.d.z);
            const double offset=finite()?std::hypot(crossing.x,crossing.y):
                std::hypot(dot(crossing,b.u),dot(crossing,b.v));
            const double scale=std::max(50.,20.*offset);
            for(int order=0;order<2001;++order) {
                const int k=order==0?0:((order+1)/2)*(order%2?-1:1);
                const double shift=scale*k/1000;
                Ray candidate;
                if(!finite()) candidate=b.launch({dx*shift,dy*shift});
                else {
                    // Fan from the fixed object point through the first-vertex
                    // plane, instead of scanning arbitrarily large direction DOF.
                    if(std::abs(fresh.d.z)<1e-14) continue;
                    const double travel=-fresh.p.z/fresh.d.z;
                    if(travel<=0) continue;
                    candidate=fresh;
                    candidate.d=normalize(fresh.d*travel+Vec3{dx*shift,dy*shift,0});
                }
                ++report.scan_candidates;
                XY hit; if(!stop(candidate,hit)) continue;
                chief=solve(candidate,{0,0},tol);
                if(chief.aim_converged) {result.strategy="scan";break;}
            }
        }
        report.chief_ms+=ms(chief_start);
        if(!chief.aim_converged) { result.chief=chief; return result; }
        const auto edge_start=aiming_clock_ns();
        result.chief=chief; result.offset=Bound(fresh,finite()).project(chief);
        Bound b(chief,finite());
        XY probes[4]; const XY targets[4]={{radius,0},{-radius,0},{0,radius},{0,-radius}};
        for(int i=0;i<4;++i) {
            Ray edge=solve(chief,targets[i],tol);
            if(!edge.aim_converged) {++report.edge_fallbacks;edge=chief;}
            probes[i]=b.project(edge);
        }
        result.affine={(probes[0][0]-probes[1][0])*.5,(probes[2][0]-probes[3][0])*.5,
                       (probes[0][1]-probes[1][1])*.5,(probes[2][1]-probes[3][1])*.5};
        result.valid=true;
        report.edge_ms+=ms(edge_start);
        return result;
    }
public:
    RobustAimingDiagnostics report;
    explicit RobustAimingState(const LensSystem& s):system(s){}
    Ray generate(const Field& field,const Wavelength& wave,const PupilPoint& pupil) {
        ++report.requests; refresh();
        const Key key{field.hx,field.hy,wave.value_um};
        const double radius=tracer->stop_target_radius(wave.value_um,RayAimingMode::Robust);
        auto it=maps.find(key);
        if(it==maps.end()) {
            const auto start=aiming_clock_ns();
            it=maps.emplace(key,calibrate(field,wave.value_um,radius)).first;
            report.calibration_ms+=ms(start);
        } else ++report.cache_hits;
        const Map& map=it->second; report.chief_strategy=map.strategy;
        Ray ray=map.chief;
        if(map.valid) {
            const auto start=aiming_clock_ns();
            const double px=field.fvcx+pupil.px*(1-field.fvdx);
            const double py=field.fvcy+pupil.py*(1-field.fvdy);
            const auto& a=map.affine;
            Ray seed=Bound(map.chief,finite()).launch({a[0]*px+a[1]*py,a[2]*px+a[3]*py});
            ray=solve(seed,{radius*px,radius*py},system.ray_aiming.tolerance);
            report.polish_ms+=ms(start);
            report.polish_iterations+=ray.aim_iterations;
        }
        ray.nominal_weight=pupil.weight; ray.intensity=ray.valid?pupil.weight:0;
        ray.aim_target_radius=radius;
        if(ray.valid&&ray.aim_converged)++report.converged;
        return ray;
    }
};

Ray AimingSession::generate_robust(const Field& field,const Wavelength& wave,const PupilPoint& pupil) {
    if(!robust_state_) robust_state_=std::make_shared<RobustAimingState>(system_);
    return robust_state_->generate(field,wave,pupil);
}

RobustAimingDiagnostics AimingSession::robust_diagnostics() const {
    return robust_state_?robust_state_->report:RobustAimingDiagnostics{};
}

} // namespace optiland
