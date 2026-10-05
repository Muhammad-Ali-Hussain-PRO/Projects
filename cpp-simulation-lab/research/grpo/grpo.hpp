#pragma once
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>
namespace groupalign {
inline void require(bool x,const char* why){if(!x)throw std::invalid_argument(why);}
// Population standard deviation, with an explicit zero-variance safeguard.
inline std::vector<double> advantages(const std::vector<double>& rewards,double epsilon=1e-8){
    require(!rewards.empty() && std::isfinite(epsilon) && epsilon>0,"invalid reward group or epsilon");
    for(double r:rewards)require(std::isfinite(r),"nonfinite reward");
    double mean=std::accumulate(rewards.begin(),rewards.end(),0.0)/rewards.size();
    double variance=0;for(double r:rewards)variance+=(r-mean)*(r-mean);
    double stdev=std::sqrt(variance/rewards.size());
    std::vector<double> result;
    for(double r:rewards)result.push_back(stdev<=epsilon?0:(r-mean)/stdev);
    return result;
}
inline double clipped_surrogate(double ratio,double advantage,double clip=0.2){
    require(std::isfinite(ratio) && ratio>0 && std::isfinite(advantage),"invalid ratio or advantage");
    require(std::isfinite(clip) && clip>0 && clip<1,"invalid clipping epsilon");
    return std::min(ratio*advantage,std::clamp(ratio,1-clip,1+clip)*advantage);
}
// Nonnegative sampled KL estimator used by the paper, NOT exact distribution KL.
inline double sampled_kl(double policy_logp,double reference_logp){
    require(std::isfinite(policy_logp) && std::isfinite(reference_logp),"invalid log probability");
    double delta=reference_logp-policy_logp;
    double estimate=std::expm1(delta)-delta;
    require(std::isfinite(estimate),"KL estimator overflow");
    return estimate;
}
struct Completion {
    std::vector<double> policy_logp,old_logp,reference_logp;
};
// Equal weighting of completions, token mean within each completion.
inline double objective(const std::vector<Completion>& group,const std::vector<double>& reward,
                        double clip=0.2,double beta=0.04){
    require(group.size()==reward.size() && !group.empty(),"group shape mismatch");
    require(std::isfinite(beta) && beta>=0,"invalid KL weight");
    auto adv=advantages(reward);double total=0;
    for(size_t i=0;i<group.size();++i){
        const auto& c=group[i];size_t n=c.policy_logp.size();
        require(n>0 && n==c.old_logp.size() && n==c.reference_logp.size(),"completion token shape mismatch");
        double sequence=0;
        for(size_t j=0;j<n;++j){
            require(std::isfinite(c.policy_logp[j]) && std::isfinite(c.old_logp[j]),"nonfinite log probability");
            double ratio=std::exp(c.policy_logp[j]-c.old_logp[j]);
            sequence+=clipped_surrogate(ratio,adv[i],clip)-beta*sampled_kl(c.policy_logp[j],c.reference_logp[j]);
        }
        total+=sequence/n;
    }
    return total/group.size();
}
}
