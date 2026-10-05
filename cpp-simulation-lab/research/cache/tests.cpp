#include "cache.hpp"
#include "../test_support.hpp"
#include <limits>
int main(){
    using namespace vectorgate;Tests t;
    Namespace scope{"tenant-a","reader","model","v1","system1","tools1","temp0","corpus1","embed1"};
    Cache c(2,2,0.95);c.put(scope,"q1",{1,0},"answer1",0,100);
    auto hit=c.get(scope,"paraphrase",{10,0.1},1);
    t.check(hit && hit->response=="answer1" && !hit->exact_request,"cosine similarity finds same namespace paraphrase");
    t.check(!c.get(scope,"unrelated",{0,1},1),"low similarity misses");
    t.check(c.get(scope,"q1",{0,1},1)->exact_request,"exact request lookup allowed within exact metadata");
    std::vector<Namespace> variants(9,scope);
    variants[0].tenant="tenant-b";variants[1].auth_scope="admin";variants[2].model="other";
    variants[3].model_revision="v2";variants[4].system_digest="system2";variants[5].tools_digest="tools2";
    variants[6].decoding_digest="temp1";variants[7].knowledge_revision="corpus2";variants[8].embedding_revision="embed2";
    for(size_t i=0;i<variants.size();++i)t.check(!c.get(variants[i],"q1",{1,0},2),"metadata field isolation "+std::to_string(i));
    c.put(scope,"q2",{0,1},"answer2",2,100);
    c.get(scope,"q1",{1,0},3); // q1 becomes MRU
    c.put(scope,"q3",{-1,0},"answer3",4,100);
    t.check(c.size()==2,"capacity is bounded");
    t.check(!c.get(scope,"q2",{0,1},5),"LRU entry evicted after recent hit");
    t.check(c.get(scope,"q1",{1,0},5)->response=="answer1","recently used entry survives eviction");
    c.put(scope,"q1",{1,0},"replacement",6,10);
    t.check(c.size()==2 && c.get(scope,"q1",{1,0},7)->response=="replacement","same exact key updates without duplicate entry");
    t.check(!c.get(scope,"q1",{1,0},10),"expiry boundary removes stale entry");
    t.rejects([&]{c.get(scope,"bad",{0,0},11);},"zero norm rejected");
    t.rejects([&]{c.get(scope,"bad",{1,2,3},11);},"dimension mismatch rejected");
    t.rejects([&]{c.get(scope,"bad",{std::numeric_limits<double>::quiet_NaN(),0},11);},"nonfinite embedding rejected");
    t.rejects([&]{c.put(scope,"expired",{1,0},"x",12,12);},"already expired insertion rejected");
    t.rejects([&]{c.get(Namespace{},"empty",{1,0},13);},"incomplete namespace metadata rejected");
    t.rejects([]{cosine({1,0},{1});},"public cosine shape mismatch rejected");
    t.near(cosine(normalize({3,4},2),normalize({6,8},2)),1,1e-12,"cosine normalization is scale invariant");
    t.report("VectorGate Cache",",\n  \"method\": \"in-process bounded cache; no network proxy\"");
}
