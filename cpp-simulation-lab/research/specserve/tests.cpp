#include "speculative.hpp"
#include "../test_support.hpp"
#include <iomanip>
#include <sstream>
int main(){
    using namespace specserve;Tests t;
    Distribution p={0.1,0.3,0.6},q={0.6,0.3,0.1};
    auto residual=correction(p,q);
    t.check(residual==Distribution({0,0,1}),"rejection correction uses normalized positive p minus q");
    double reject_mass=0;
    for(size_t i=0;i<p.size();++i)reject_mass+=q[i]-std::min(p[i],q[i]);
    for(size_t i=0;i<p.size();++i)t.near(std::min(p[i],q[i])+reject_mass*residual[i],p[i],1e-12,"analytic output mass equals target "+std::to_string(i));
    Model same=[](const Tokens&){return Distribution{0.2,0.3,0.5};};
    std::mt19937_64 rng(20261005);
    auto accepted=round({},same,same,4,rng);
    t.check(accepted.accepted==4 && !accepted.rejected && accepted.emitted.size()==5,"identical models accept all proposals and add bonus token");
    Model only_zero=[](const Tokens&){return Distribution{1,0};};
    Model only_one=[](const Tokens&){return Distribution{0,1};};
    auto rejected=round({},only_zero,only_one,3,rng);
    t.check(rejected.rejected && rejected.accepted==0 && rejected.emitted==Tokens({0}),"disjoint support rejects draft and corrects to target");
    t.rejects([]{validate({0.2,0.2});},"unnormalized distribution rejected");
    t.rejects([]{correction({1,0},{1});},"vocabulary mismatch rejected");
    t.rejects([&]{round({},same,same,0,rng);},"zero draft length rejected");
    // Context-dependent target tests entire two-token joint distribution.
    Model target=[](const Tokens& prefix){
        if(prefix.empty())return Distribution{0.15,0.35,0.50};
        static const std::vector<Distribution> rows={{0.8,0.1,0.1},{0.1,0.7,0.2},{0.3,0.2,0.5}};
        return rows.at(prefix.back());
    };
    Model draft=[](const Tokens& prefix){
        if(prefix.empty())return Distribution{0.6,0.35,0.05};
        return prefix.back()==0 ? Distribution{0.1,0.4,0.5} : Distribution{0.6,0.3,0.1};
    };
    const size_t samples=200000;std::vector<size_t> counts(9,0);
    for(size_t i=0;i<samples;++i){auto tokens=generate(2,target,draft,3,rng);++counts[tokens[0]*3+tokens[1]];}
    double maximum_error=0;
    auto initial=target({});
    for(size_t a=0;a<3;++a)for(size_t b=0;b<3;++b){
        double expected=initial[a]*target({a})[b];
        double observed=static_cast<double>(counts[a*3+b])/samples;
        maximum_error=std::max(maximum_error,std::abs(observed-expected));
        t.near(observed,expected,0.004,"context-dependent joint probability "+std::to_string(a)+std::to_string(b));
    }
    std::ostringstream detail;detail<<",\n  \"monte_carlo\": {\"seed\":20261005,\"sequences\":"<<samples
        <<",\"absolute_tolerance\":0.004,\"maximum_joint_error\":"<<std::setprecision(10)<<maximum_error<<"}";
    t.report("SpecServe",detail.str());
}
