#pragma once
#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <random>
#include <stdexcept>
#include <vector>
namespace specserve {
using Distribution=std::vector<double>;
using Tokens=std::vector<size_t>;
using Model=std::function<Distribution(const Tokens&)>;
inline void validate(const Distribution& p){
    if(p.empty())throw std::invalid_argument("empty distribution");
    double sum=0;
    for(double x:p){if(!std::isfinite(x) || x<0)throw std::invalid_argument("invalid probability");sum+=x;}
    if(std::abs(sum-1)>1e-10)throw std::invalid_argument("probabilities must sum to one");
}
inline double uniform(std::mt19937_64& rng){return std::generate_canonical<double,53>(rng);}
inline size_t categorical(const Distribution& p,std::mt19937_64& rng){
    validate(p);double u=uniform(rng), cumulative=0;
    for(size_t i=0;i<p.size();++i){cumulative+=p[i];if(u<cumulative)return i;}
    // Rounding at the upper boundary must never select a zero-probability token.
    for(size_t i=p.size();i>0;--i)if(p[i-1]>0)return i-1;
    throw std::invalid_argument("distribution has no support");
}
inline Distribution correction(const Distribution& p,const Distribution& q){
    validate(p);validate(q);
    if(p.size()!=q.size())throw std::invalid_argument("vocabulary mismatch");
    Distribution r(p.size());double mass=0;
    for(size_t i=0;i<p.size();++i){r[i]=std::max(0.0,p[i]-q[i]);mass+=r[i];}
    if(mass<=0)throw std::invalid_argument("correction has no mass; rejection is impossible");
    for(double& x:r)x/=mass;
    return r;
}
struct Round {Tokens emitted;size_t accepted=0;bool rejected=false;};
// Exact rejection/correction rule, simulated on toy probability callbacks.
// Target callbacks run sequentially here; this is not a batched serving engine.
inline Round round(const Tokens& prefix,const Model& target,const Model& draft,
                   size_t gamma,std::mt19937_64& rng){
    if(gamma==0 || gamma>1024)throw std::invalid_argument("invalid draft length");
    Tokens guessed=prefix;std::vector<Distribution> qs;Tokens proposals;
    for(size_t i=0;i<gamma;++i){
        auto q=draft(guessed);validate(q);
        size_t token=categorical(q,rng);
        qs.push_back(q);proposals.push_back(token);guessed.push_back(token);
    }
    Round result;Tokens actual=prefix;
    for(size_t i=0;i<gamma;++i){
        auto p=target(actual);validate(p);
        if(p.size()!=qs[i].size())throw std::invalid_argument("vocabulary mismatch");
        size_t token=proposals[i];double acceptance=std::min(1.0,p[token]/qs[i][token]);
        if(uniform(rng)<acceptance){
            result.emitted.push_back(token);actual.push_back(token);++result.accepted;
        }else{
            result.emitted.push_back(categorical(correction(p,qs[i]),rng));
            result.rejected=true;return result;
        }
    }
    result.emitted.push_back(categorical(target(actual),rng));
    return result;
}
inline Tokens generate(size_t count,const Model& target,const Model& draft,size_t gamma,std::mt19937_64& rng){
    Tokens output;
    while(output.size()<count){auto r=round(output,target,draft,gamma,rng);
        for(size_t token:r.emitted){if(output.size()==count)break;output.push_back(token);}}
    return output;
}
} // namespace specserve
