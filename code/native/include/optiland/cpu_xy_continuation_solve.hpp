#pragma once
#include <cmath>

// Same 2-D scalar-inverse Broyden recurrence and two backtracks as CUDA's
// existing fast edge solver. The callbacks retain CPU STOP/root/bounds policy.
template<class Seed, class Probe, class Bounds>
bool cpu_xy_continuation_solve(Seed seed, double sx, double sy,
    double target_x, double target_y, double limit, double initial_scale,
    int iterations, Probe probe, Bounds bounds,
    Seed* result, double* result_sx, double* result_sy) {
    if (!seed.valid || !bounds(seed) || !std::isfinite(sx) || !std::isfinite(sy)) return false;
    Seed previous=seed,last=seed;
    double previous_ex=0.0,previous_ey=0.0,last_sx=sx,last_sy=sy;
    double h11=initial_scale,h12=0.0,h21=0.0,h22=initial_scale;
    bool have_previous=false,use_initial=true;
    for (int iteration=0;iteration<iterations;++iteration) {
        if (!use_initial) {
            bool ok=bounds(seed) && probe(seed,&sx,&sy);
            if (!ok && have_previous) {
                const Seed rejected=seed;
                double fraction=0.5;
                for (int back=0;back<2 && !ok;++back,fraction*=0.5) {
                    Seed trial=previous;
                    trial.x=previous.x+fraction*(rejected.x-previous.x);
                    trial.y=previous.y+fraction*(rejected.y-previous.y);
                    trial.valid=std::isfinite(trial.x) && std::isfinite(trial.y);
                    if (bounds(trial) && probe(trial,&sx,&sy)) {seed=trial;ok=true;}
                }
            }
            if (!ok) return false;
        }
        use_initial=false;last=seed;last_sx=sx;last_sy=sy;
        const double ex=sx-target_x,ey=sy-target_y;
        const double residual=std::hypot(ex,ey);
        if (!std::isfinite(residual)) return false;
        if (residual<=limit) {*result=seed;*result_sx=sx;*result_sy=sy;return true;}
        if (have_previous) {
            const double dx=seed.x-previous.x,dy=seed.y-previous.y;
            const double yx=ex-previous_ex,yy=ey-previous_ey;
            const double hyx=h11*yx+h12*yy,hyy=h21*yx+h22*yy;
            const double ux=dx-hyx,uy=dy-hyy;
            const double denominator=yx*yx+yy*yy;
            if (std::isfinite(denominator) && denominator>1.0e-24) {
                const double scale=1.0/denominator;
                h11+=ux*yx*scale;h12+=ux*yy*scale;
                h21+=uy*yx*scale;h22+=uy*yy*scale;
            }
        }
        previous=seed;previous_ex=ex;previous_ey=ey;have_previous=true;
        const double step_x=h11*ex+h12*ey,step_y=h21*ex+h22*ey;
        if (!std::isfinite(step_x) || !std::isfinite(step_y)) return false;
        seed.x-=step_x;seed.y-=step_y;
    }
    if (!(seed.x==last.x && seed.y==last.y)) {
        if (!bounds(seed) || !probe(seed,&last_sx,&last_sy)) return false;
    }
    const double residual=std::hypot(last_sx-target_x,last_sy-target_y);
    if (std::isfinite(residual) && residual<=limit) {
        *result=seed;*result_sx=last_sx;*result_sy=last_sy;return true;
    }
    return false;
}
