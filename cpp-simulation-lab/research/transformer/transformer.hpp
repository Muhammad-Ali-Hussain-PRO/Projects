#pragma once
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace forge {
using Vec = std::vector<double>;
using Matrix = std::vector<Vec>; // [output dimension][input dimension]
using Heads = std::vector<Vec>;
using SequenceHeads = std::vector<Heads>; // [token][head][head dimension]

inline void require(bool ok, const char* why) {
    if (!ok) throw std::invalid_argument(why);
}
inline bool finite(const Vec& v) {
    return std::all_of(v.begin(), v.end(), [](double x){ return std::isfinite(x); });
}
inline double dot(const Vec& a, const Vec& b) {
    require(a.size()==b.size(), "dot shape mismatch");
    double s=0;
    for (size_t i=0;i<a.size();++i) s+=a[i]*b[i];
    return s;
}
// Adjacent-pair convention. Frequency for pair j is base^(-2j/d).
// A checkpoint using split-half pairing would require an explicit conversion.
inline Vec rope(Vec x, long long position, double base=10000.0) {
    require(!x.empty() && x.size()%2==0 && finite(x), "RoPE requires a finite even dimension");
    require(std::isfinite(base) && base>1, "invalid RoPE base");
    for (size_t i=0;i<x.size();i+=2) {
        double angle=static_cast<double>(position)*std::pow(base,-static_cast<double>(i)/x.size());
        double c=std::cos(angle), s=std::sin(angle), a=x[i], b=x[i+1];
        x[i]=a*c-b*s; x[i+1]=a*s+b*c;
    }
    return x;
}
inline Vec matvec(const Matrix& w, const Vec& x) {
    require(!w.empty() && finite(x), "empty matrix or nonfinite input");
    Vec y;
    for (const Vec& row:w) {
        require(row.size()==x.size() && finite(row), "matrix shape or value invalid");
        y.push_back(dot(row,x));
    }
    return y;
}
inline Vec rmsnorm(const Vec& x, double eps=1e-6) {
    require(!x.empty() && finite(x) && std::isfinite(eps) && eps>0, "invalid RMSNorm input");
    double scale=1/std::sqrt(dot(x,x)/x.size()+eps);
    Vec y=x;
    for (double& v:y) v*=scale;
    return y;
}
inline double silu(double x) {
    // Two branches avoid exp overflow for a large negative activation.
    return x>=0 ? x/(1+std::exp(-x)) : x*std::exp(x)/(1+std::exp(x));
}
inline Vec swiglu(const Vec& x,const Matrix& gate,const Matrix& up,const Matrix& down) {
    Vec g=matvec(gate,x), u=matvec(up,x);
    require(g.size()==u.size(), "SwiGLU hidden size mismatch");
    for (size_t i=0;i<g.size();++i) g[i]=silu(g[i])*u[i];
    return matvec(down,g);
}
inline SequenceHeads causal_gqa(SequenceHeads q, SequenceHeads k, const SequenceHeads& v) {
    require(!q.empty() && q.size()==k.size() && q.size()==v.size(), "sequence shape mismatch");
    size_t nq=q[0].size(), nk=k[0].size();
    require(nq>0 && nk>0 && nq%nk==0, "query heads must divide into KV groups");
    size_t d=q[0][0].size();
    require(d>0 && d%2==0, "head dimension must be even");
    for(size_t t=0;t<q.size();++t) {
        require(q[t].size()==nq && k[t].size()==nk && v[t].size()==nk, "head count mismatch");
        for(size_t h=0;h<nq;++h) {
            require(q[t][h].size()==d, "query shape mismatch");
            q[t][h]=rope(q[t][h],static_cast<long long>(t));
        }
        for(size_t h=0;h<nk;++h) {
            require(k[t][h].size()==d && v[t][h].size()==d && finite(v[t][h]), "KV shape mismatch");
            k[t][h]=rope(k[t][h],static_cast<long long>(t));
        }
    }
    SequenceHeads out(q.size(),Heads(nq,Vec(d,0)));
    for(size_t t=0;t<q.size();++t) for(size_t h=0;h<nq;++h) {
        size_t kh=h/(nq/nk); // contiguous groups of query heads share K and V
        Vec scores(t+1);
        for(size_t s=0;s<=t;++s) scores[s]=dot(q[t][h],k[s][kh])/std::sqrt(static_cast<double>(d));
        double mx=*std::max_element(scores.begin(),scores.end()), sum=0;
        for(double& score:scores) { score=std::exp(score-mx); sum+=score; }
        for(size_t s=0;s<=t;++s) for(size_t i=0;i<d;++i) out[t][h][i]+=scores[s]/sum*v[s][kh][i];
    }
    return out;
}
struct Config { size_t model, query_heads, kv_heads, head_dim; };
struct Weights { Matrix q,k,v,out,gate,up,down; };
inline Heads split(const Vec& x,size_t heads,size_t d) {
    require(x.size()==heads*d, "projection size mismatch");
    Heads y(heads,Vec(d));
    for(size_t h=0;h<heads;++h) std::copy_n(x.begin()+h*d,d,y[h].begin());
    return y;
}
// Tiny pre-norm residual forward block; RMSNorm scales are fixed to one.
inline Matrix forward(const Matrix& input,const Config& c,const Weights& w) {
    require(c.model>0 && c.query_heads>0 && c.kv_heads>0 && c.head_dim>0, "invalid config");
    SequenceHeads q,k,v;
    for(const Vec& x:input) {
        require(x.size()==c.model, "model dimension mismatch");
        Vec n=rmsnorm(x);
        q.push_back(split(matvec(w.q,n),c.query_heads,c.head_dim));
        k.push_back(split(matvec(w.k,n),c.kv_heads,c.head_dim));
        v.push_back(split(matvec(w.v,n),c.kv_heads,c.head_dim));
    }
    auto attn=causal_gqa(q,k,v);
    Matrix result=input;
    for(size_t t=0;t<input.size();++t) {
        Vec joined;
        for(const Vec& head:attn[t]) joined.insert(joined.end(),head.begin(),head.end());
        Vec a=matvec(w.out,joined);
        require(a.size()==c.model, "output projection mismatch");
        for(size_t i=0;i<c.model;++i) result[t][i]+=a[i];
        Vec f=swiglu(rmsnorm(result[t]),w.gate,w.up,w.down);
        require(f.size()==c.model, "feedforward output mismatch");
        for(size_t i=0;i<c.model;++i) result[t][i]+=f[i];
    }
    return result;
}
} // namespace forge
