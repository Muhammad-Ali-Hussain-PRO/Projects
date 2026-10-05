#include "windrunner.hpp"
#include <iostream>
using namespace wr;
void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
void near(double a,double b,double tol,const char* what){check(std::abs(a-b)<=tol,what);}
int main(){try{
    Environment a(17),b(17);check(a.phases==b.phases,"same-seed phases differ");
    Environment different(18);check(a.phases!=different.phases,"different-seed phases equal");
    for(int i=0;i<35&&!a.done;++i){Action u={.23,.36};auto ra=a.step(u),rb=b.step(u);near(a.s.x,b.s.x,0,"deterministic x");near(a.s.y,b.s.y,0,"deterministic y");near(ra.reward,rb.reward,0,"deterministic reward");}
    for(uint32_t seed=1;seed<101;++seed){Environment env(seed);for(int i=0;i<400&&!env.done;++i){env.step({.4,.31});for(double x:env.observe())check(std::isfinite(x),"nonfinite observation");check(env.half_width(env.s.x)>=2.35&&env.half_width(env.s.x)<=2.95,"cave width bounds");}check(env.done,"episode did not terminate");check(env.s.step<=400,"horizon exceeded");}
    Environment clipped(42),bounded(42);clipped.step({100,-100});bounded.step({1,-1});near(clipped.s.x,bounded.s.x,0,"action x clipping");near(clipped.s.y,bounded.s.y,0,"action y clipping");
    Environment physics(42);auto old=physics.s;auto wind=physics.wind();physics.step({.2,.4});double vx=old.vx+DT*(.6+wind[0]-.25*old.vx),vy=old.vy+DT*(1.6+wind[1]-1.2-.32*old.vy);near(physics.s.vx,vx,1e-14,"horizontal acceleration");near(physics.s.vy,vy,1e-14,"vertical acceleration");near(physics.s.x,old.x+DT*vx,1e-14,"semi-implicit horizontal integration");near(physics.s.y,old.y+DT*vy,1e-14,"semi-implicit vertical integration");
    Environment collision(12);collision.s.y=collision.center(0)+collision.half_width(0);auto hit=collision.step({0,0});check(hit.terminated&&hit.terminal=="collision"&&hit.reward<-29,"collision penalty/termination");
    bool threw=false;try{collision.step({0,0});}catch(const std::logic_error&){threw=true;}check(threw,"terminal step must reject");
    Environment goal(12);goal.s.x=64;goal.s.y=goal.center(64);auto win=goal.step({0,.3});check(win.terminated&&win.terminal=="goal"&&win.reward>24,"goal bonus/termination");
    Environment timeout(12);timeout.s.step=399;timeout.s.t=399*DT;auto end=timeout.step({0,.3});check(end.truncated&&!end.terminated&&end.terminal=="timeout","timeout must truncate");
    Policy p;Observation o={1,.2,.3,-.1,.25,.4,-.2,.13,.2,.6};Action raw={.31,-.42};for(int j=0;j<ACT*OBS;++j)p.p[j]=.02*(j-8);
    auto score=p.score(o,raw);double eps=1e-6;
    for(int j=0;j<PARAMS;++j){Policy plus=p,minus=p;plus.p[j]+=eps;minus.p[j]-=eps;double fd=(plus.log_probability(o,raw)-minus.log_probability(o,raw))/(2*eps);near(score[j],fd,2e-7,"Gaussian score finite difference");}
    for(double adv:{-1.3,1.7})for(double lr:{-.4,-.1,.1,.4}){double fd=(surrogate(lr+eps,adv)-surrogate(lr-eps,adv))/(2*eps);near(surrogate_log_gradient(lr,adv),fd,2e-7,"PPO clipping finite difference");}
    check(surrogate_log_gradient(std::log(1.4),1)==0,"positive advantage upper clipping");check(surrogate_log_gradient(std::log(.6),-1)==0,"negative advantage lower clipping");
    check(surrogate_log_gradient(std::log(.6),1)>0,"beneficial unclipped lower ratio");check(surrogate_log_gradient(std::log(1.4),-1)<0,"beneficial unclipped upper ratio");
    std::vector<Sample> samples(3);samples[0].reward=1;samples[0].value=.5;samples[0].next_value=.6;samples[1].reward=2;samples[1].value=.6;samples[1].ended=true;samples[1].next_value=0;samples[2].reward=100;samples[2].ended=true;compute_gae(samples,1,1);near(samples[0].adv,2.5,1e-12,"GAE terminal boundary");near(samples[1].target,2,1e-12,"GAE terminal target");
    std::vector<Sample> truncation(2);truncation[0].reward=1;truncation[0].value=.3;truncation[0].next_value=.7;truncation[0].ended=true;truncation[1].reward=100;truncation[1].ended=true;compute_gae(truncation,.9,.95);near(truncation[0].target,1.63,1e-12,"GAE truncation bootstrap without reset leakage");
    std::cout<<"PASS: environment determinism, clipping, physics bounds, terminal semantics, Gaussian/PPO finite-difference gradients, and GAE boundaries\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
