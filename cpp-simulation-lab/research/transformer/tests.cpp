#include "transformer.hpp"
#include "../test_support.hpp"
#include <limits>

forge::Matrix weights(size_t rows,size_t cols,double seed) {
    forge::Matrix w(rows,forge::Vec(cols));
    for(size_t i=0;i<rows;++i) for(size_t j=0;j<cols;++j)
        w[i][j]=0.1*std::sin(seed+i*3+j);
    return w;
}
int main() {
    using namespace forge;
    Tests t;
    Vec x={1,2,3,4};
    t.check(rope(x,0)==x,"RoPE position zero is identity");
    Vec r=rope({1,0,0,1},1);
    t.near(r[0],std::cos(1.0),1e-12,"RoPE first pair frequency");
    t.near(r[1],std::sin(1.0),1e-12,"RoPE first pair orientation");
    t.near(r[2],-std::sin(0.01),1e-12,"RoPE second pair frequency");
    t.near(dot(rope(x,37),rope(x,37)),dot(x,x),1e-12,"RoPE preserves squared norm");
    Vec y={-1,0.5,2,3};
    t.near(dot(rope(x,7),rope(y,3)),dot(x,rope(y,-4)),1e-12,"RoPE dot product depends on relative position");
    t.rejects([]{rope({1,2,3},0);},"odd RoPE dimension rejected");
    Matrix ident={{1,0},{0,1}};
    Vec f=swiglu({1,-1},ident,ident,ident);
    t.near(f[0],1/(1+std::exp(-1.0)),1e-12,"SwiGLU positive hand calculation");
    t.near(f[1],1/(1+std::exp(1.0)),1e-12,"SwiGLU negative hand calculation");
    SequenceHeads q(2,Heads(4,Vec(2,0))), k(2,Heads(2,Vec(2,0)));
    SequenceHeads v={{{1,2},{10,20}},{{3,4},{30,40}}};
    auto out=causal_gqa(q,k,v);
    t.check(out[0][0]==Vec({1,2}) && out[0][3]==Vec({10,20}),"GQA group mapping at first token");
    t.check(out[1][0]==Vec({2,3}) && out[1][3]==Vec({20,30}),"GQA uniform scores average causal values");
    t.check(out[0][0]==Vec({1,2}),"future values excluded by causal mask");
    t.rejects([&]{causal_gqa(SequenceHeads(2,Heads(3,Vec(2))),k,v);},"nondivisible head grouping rejected");
    Config c{8,4,2,2};
    Weights w{weights(8,8,1),weights(4,8,2),weights(4,8,3),weights(8,8,4),
              weights(12,8,5),weights(12,8,6),weights(8,12,7)};
    Matrix input={{0.1,0.2,-0.3,0.4,0.5,0.6,0.7,0.8},{0.8,0.7,0.6,0.5,0.4,-0.3,0.2,0.1}};
    auto all=forward(input,c,w), prefix=forward({input[0]},c,w);
    t.check(all.size()==2 && all[0].size()==8 && finite(all[0]) && finite(all[1]),"full forward shape and finite values");
    t.check(all[0]==prefix[0],"full block causal prefix invariance");
    t.check(all[0]!=input[0],"attention and feedforward change residual stream");
    t.report("Transformer Forge",",\n  \"fixture\": {\"model_dimension\":8,\"query_heads\":4,\"kv_heads\":2,\"head_dimension\":2}");
}
