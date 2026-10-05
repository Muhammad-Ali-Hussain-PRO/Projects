#include "grpo.hpp"
#include "../test_support.hpp"
#include <limits>
int main(){
    using namespace groupalign;Tests t;
    auto a=advantages({1,2,3});
    t.near(a[0],-std::sqrt(1.5),1e-12,"population normalized low reward");
    t.near(a[1],0,1e-12,"mean reward has zero advantage");
    t.near(a[2],std::sqrt(1.5),1e-12,"population normalized high reward");
    t.near(std::accumulate(a.begin(),a.end(),0.0),0,1e-12,"group advantages sum to zero");
    t.check(advantages({7,7,7})==std::vector<double>({0,0,0}),"constant reward group safely returns zeros");
    auto shifted=advantages({12,14,16});
    for(size_t i=0;i<a.size();++i)t.near(shifted[i],a[i],1e-12,"positive reward affine invariance "+std::to_string(i));
    t.near(clipped_surrogate(1.5,2),2.4,1e-12,"positive advantage upper clip");
    t.near(clipped_surrogate(0.5,2),1,1e-12,"positive advantage lower side remains pessimistic");
    t.near(clipped_surrogate(0.5,-2),-1.6,1e-12,"negative advantage lower clip");
    t.near(clipped_surrogate(1.5,-2),-3,1e-12,"negative advantage upper side remains pessimistic");
    t.near(sampled_kl(-2,-2),0,1e-12,"identical log probabilities have zero sampled KL");
    t.near(sampled_kl(-2,-1),std::exp(1.0)-2,1e-12,"sampled KL hand calculation");
    Completion low{{std::log(0.1)},{std::log(0.2)},{std::log(0.1)}};
    Completion high{{std::log(0.3),std::log(0.3)},{std::log(0.2),std::log(0.2)},{std::log(0.3),std::log(0.3)}};
    t.near(objective({low,high},{0,1},0.2,0),0.2,1e-12,"objective uses completion means with unequal token lengths");
    t.rejects([]{advantages({});},"empty reward group rejected");
    t.rejects([]{clipped_surrogate(0,1);},"zero importance ratio rejected");
    t.rejects([]{advantages({std::numeric_limits<double>::quiet_NaN()});},"nonfinite reward rejected");
    t.rejects([&]{objective({low},{0,1});},"mismatched group shape rejected");
    t.report("GroupAlign GRPO",",\n  \"method\": \"numerical objective evaluation; no policy training\"");
}
