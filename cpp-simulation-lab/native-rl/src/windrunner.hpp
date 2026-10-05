#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wr {
constexpr int OBS = 10, ACT = 2, PARAMS = ACT * OBS + ACT, VF = OBS * 2 - 1;
constexpr double DT = .08, GOAL = 64., RADIUS = .22, PI = 3.14159265358979323846;
constexpr int HORIZON = 400;
using Observation = std::array<double, OBS>;
using Action = std::array<double, ACT>;
using Parameters = std::array<double, PARAMS>;
inline double clip(double x, double lo, double hi) { return std::max(lo, std::min(hi, x)); }

// The environment PRNG is specified independently of the C++ standard library.
struct XorShift32 {
    uint32_t state;
    explicit XorShift32(uint32_t seed): state(seed ? seed : 0x6d2b79f5u) {}
    uint32_t next() { state ^= state << 13; state ^= state >> 17; state ^= state << 5; return state; }
    double uniform() { return double(next()) / 4294967296.; }
};

struct State { double x=0, y=0, vx=0, vy=0, t=0; int step=0; };
struct Transition { double reward=0; bool terminated=false, truncated=false; std::string terminal="running"; };
class Environment {
public:
    State s;
    std::array<double,4> phases{};
    uint32_t seed = 0;
    bool done = false;
    double episode_return = 0;
    std::string terminal = "running";

    explicit Environment(uint32_t seed_=1) { reset(seed_); }
    Observation reset(uint32_t seed_) {
        seed = seed_; XorShift32 rng(seed);
        for (auto &p : phases) p = 2*PI*rng.uniform();
        s = {}; s.y = center(0) + (rng.uniform()-.5)*.6; s.vx=.8;
        done=false; terminal="running"; episode_return=0;
        return observe();
    }
    double center(double x) const {
        return .9*std::sin(.105*x+phases[0])+.45*std::sin(.29*x+phases[1])+.25*std::sin(.53*x+phases[2]);
    }
    double slope(double x) const {
        return .0945*std::cos(.105*x+phases[0])+.1305*std::cos(.29*x+phases[1])+.1325*std::cos(.53*x+phases[2]);
    }
    double half_width(double x) const { return 2.65+.3*std::sin(.18*x+phases[3]); }
    Action wind() const {
        double offset=s.y-center(s.x);
        return {.6*std::sin(.24*s.x+phases[1]+.35*s.t)+.13*offset,
                .85*std::sin(.2*s.x+phases[2]+.8*s.t)+.4*std::sin(.62*s.x+phases[0])-.12*offset};
    }
    Observation observe() const {
        Action w=wind(); double c=center(s.x);
        return {1.,(s.y-c)/half_width(s.x),s.vx/4.,s.vy/3.,slope(s.x),
                (center(s.x+6)-c)/3.,w[0]/1.5,w[1]/1.5,s.x/GOAL,double(HORIZON-s.step)/HORIZON};
    }
    Transition step(Action action) {
        if (done) throw std::logic_error("step after terminal state; call reset");
        for(auto &a:action) { if(!std::isfinite(a)) throw std::invalid_argument("non-finite action"); a=clip(a,-1,1); }
        Action w=wind(); double old_x=s.x;
        s.vx += DT*(3*action[0]+w[0]-.25*s.vx);
        s.vy += DT*(4*action[1]+w[1]-1.2-.32*s.vy);
        s.x += DT*s.vx; s.y += DT*s.vy; ++s.step; s.t = DT*s.step;
        double offset=(s.y-center(s.x))/half_width(s.x);
        Transition result;
        result.reward=1.2*(s.x-old_x)+.08-.06*offset*offset-.015*(action[0]*action[0]+action[1]*action[1])-.003*s.vy*s.vy;
        if (std::abs(s.y-center(s.x))+RADIUS>=half_width(s.x) || s.x < -1) {
            result.terminated=true; result.terminal="collision"; result.reward-=30;
        } else if (s.x>=GOAL) {
            result.terminated=true; result.terminal="goal"; result.reward+=25;
        } else if (s.step>=HORIZON) {
            result.truncated=true; result.terminal="timeout"; result.reward-=5;
        }
        done=result.terminated||result.truncated; terminal=result.terminal; episode_return+=result.reward;
        return result;
    }
};

struct Policy {
    Parameters p{};
    Policy() { p[ACT*OBS]=p[ACT*OBS+1]=std::log(.55); }
    Action mean(const Observation& o) const {
        Action mu{};
        for(int a=0;a<ACT;++a) for(int j=0;j<OBS;++j) mu[a]+=p[a*OBS+j]*o[j];
        return mu;
    }
    double log_probability(const Observation& o,const Action& raw) const {
        Action mu=mean(o); double lp=0;
        for(int a=0;a<ACT;++a) {
            double ls=p[ACT*OBS+a], z=(raw[a]-mu[a])*std::exp(-ls);
            lp+=-.5*z*z-ls-.5*std::log(2*PI);
        }
        return lp;
    }
    Parameters score(const Observation& o,const Action& raw) const {
        Action mu=mean(o); Parameters g{};
        for(int a=0;a<ACT;++a) {
            double d=raw[a]-mu[a], invvar=std::exp(-2*p[ACT*OBS+a]);
            for(int j=0;j<OBS;++j) g[a*OBS+j]=d*invvar*o[j];
            g[ACT*OBS+a]=d*d*invvar-1;
        }
        return g;
    }
    Action sample(const Observation& o,std::mt19937_64& rng) const {
        std::normal_distribution<double> normal(0,1); Action raw=mean(o);
        for(int a=0;a<ACT;++a) raw[a]+=std::exp(p[ACT*OBS+a])*normal(rng);
        return raw;
    }
    Action act(const Observation& o) const { Action a=mean(o); for(auto &x:a)x=clip(x,-1,1); return a; }
};

inline std::array<double,VF> value_features(const Observation& o) {
    std::array<double,VF> f{};
    for(int j=0;j<OBS;++j) f[j]=o[j];
    for(int j=1;j<OBS;++j) f[OBS+j-1]=o[j]*o[j];
    return f;
}
struct Value {
    std::array<double,VF> p{};
    double predict(const Observation& o) const { auto f=value_features(o); return std::inner_product(p.begin(),p.end(),f.begin(),0.); }
};

// Derivative of min(r*A, clip(r)*A) with respect to log probability.
inline double surrogate(double log_ratio,double advantage,double epsilon=.2) {
    double r=std::exp(clip(log_ratio,-20,20));
    return std::min(r*advantage,clip(r,1-epsilon,1+epsilon)*advantage);
}
inline double surrogate_log_gradient(double log_ratio,double advantage,double epsilon=.2) {
    double r=std::exp(clip(log_ratio,-20,20));
    bool flat=(advantage>=0 && r>1+epsilon)||(advantage<0 && r<1-epsilon);
    return flat ? 0 : r*advantage;
}

template<size_t N> struct Adam {
    std::array<double,N> m{},v{}; int t=0;
    void update(std::array<double,N>& p,std::array<double,N> g,double lr,double max_norm=1.) {
        double norm=std::sqrt(std::inner_product(g.begin(),g.end(),g.begin(),0.));
        if(norm>max_norm)for(auto &x:g)x*=max_norm/norm;
        ++t; double b1=1-std::pow(.9,t),b2=1-std::pow(.999,t);
        for(size_t j=0;j<N;++j) {
            m[j]=.9*m[j]+.1*g[j]; v[j]=.999*v[j]+.001*g[j]*g[j];
            p[j]+=lr*(m[j]/b1)/(std::sqrt(v[j]/b2)+1e-8);
        }
    }
};

struct Sample { Observation obs{}; Action raw{}; double old_logp=0,value=0,next_value=0,reward=0,adv=0,target=0; bool ended=false; };
inline void compute_gae(std::vector<Sample>& samples,double gamma=.995,double lambda=.95) {
    double next_adv=0;
    for(int i=int(samples.size())-1;i>=0;--i) {
        auto &s=samples[i]; double keep=s.ended?0.:1.;
        double delta=s.reward+gamma*s.next_value-s.value;
        s.adv=delta+gamma*lambda*keep*next_adv; s.target=s.adv+s.value; next_adv=s.adv;
    }
}

inline std::string number(double x) { std::ostringstream out; out<<std::setprecision(12)<<x; return out.str(); }
inline std::string frame_json(const Environment& env,Action action,double reward) {
    const auto &s=env.s; auto w=env.wind(); std::ostringstream o; o<<std::setprecision(12);
    o<<"{\"step\":"<<s.step<<",\"t\":"<<s.t<<",\"x\":"<<s.x<<",\"y\":"<<s.y
     <<",\"vx\":"<<s.vx<<",\"vy\":"<<s.vy<<",\"center\":"<<env.center(s.x)<<",\"halfWidth\":"<<env.half_width(s.x)
     <<",\"windX\":"<<w[0]<<",\"windY\":"<<w[1]<<",\"action\":["<<clip(action[0],-1,1)<<","<<clip(action[1],-1,1)
     <<"],\"reward\":"<<reward<<",\"return\":"<<env.episode_return<<",\"done\":"<<(env.done?"true":"false")
     <<",\"terminal\":\""<<env.terminal<<"\"}";
    return o.str();
}
}
