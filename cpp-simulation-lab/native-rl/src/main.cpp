#include "windrunner.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <unordered_map>
using namespace wr;

namespace {
struct Options {
    uint32_t seed=42; int steps=400,episodes=64; bool train=false,evaluate=false,json=false,random=false,zero=false;
    std::string model="artifacts/model.json",out="artifacts";
};
Options parse(int argc,char**argv) {
    Options o;
    for(int i=1;i<argc;++i) {
        std::string a=argv[i];
        auto next=[&](){ if(++i>=argc)throw std::runtime_error("missing argument for "+a); return std::string(argv[i]); };
        if(a=="--seed") {
            uint64_t seed=std::stoull(next());
            if(seed>4294967295ull)throw std::runtime_error("seed exceeds uint32 range");
            o.seed=uint32_t(seed);
        }
        else if(a=="--steps")o.steps=std::stoi(next());
        else if(a=="--episodes")o.episodes=std::stoi(next());
        else if(a=="--model")o.model=next();
        else if(a=="--out")o.out=next();
        else if(a=="--train")o.train=true;
        else if(a=="--evaluate")o.evaluate=true;
        else if(a=="--json")o.json=true;
        else if(a=="--random")o.random=true;
        else if(a=="--zero")o.zero=true;
        else if(a=="--help") {
            std::cout<<"WindRunner RL — C++17 continuous-control PPO prototype\n"
                "  windrunner --train --seed 42 --steps 262144 --out artifacts\n"
                "  windrunner --evaluate --episodes 64 --model artifacts/model.json --json\n"
                "  windrunner --seed 2147483659 --steps 400 --json [--random|--zero]\n"
                "  --model PATH loads trained policy; --out DIR selects saved artifacts.\n"; std::exit(0);
        } else throw std::runtime_error("unknown option: "+a);
    }
    if(o.steps<=0||o.episodes<=0)throw std::runtime_error("steps and episodes must be positive");
    if(o.random&&o.zero)throw std::runtime_error("choose one baseline");
    if(o.train&&o.evaluate)throw std::runtime_error("choose train or evaluate");
    if((o.train||o.evaluate)&&(o.random||o.zero))throw std::runtime_error("--random/--zero select trajectory baselines only");
    return o;
}
void write(const std::string &path,const std::string &content) {
    auto parent=std::filesystem::path(path).parent_path(); if(!parent.empty())std::filesystem::create_directories(parent);
    std::ofstream f(path); if(!f)throw std::runtime_error("cannot write "+path); f<<content<<'\n';
}
std::vector<double> array_from_json(const std::string& text,const std::string& key) {
    size_t start=text.find("\""+key+"\""); if(start==std::string::npos)throw std::runtime_error("missing model field "+key);
    start=text.find('[',start); if(start==std::string::npos)throw std::runtime_error("invalid model array");
    size_t end=start; int depth=0;
    do { if(text[end]=='[')++depth; else if(text[end]==']')--depth; ++end; } while(depth&&end<text.size());
    if(depth)throw std::runtime_error("unterminated model array");
    std::string section=text.substr(start,end-start);
    std::regex re(R"([-+]?[0-9]*\.?[0-9]+([eE][-+]?[0-9]+)?)"); std::vector<double> a;
    for(std::sregex_iterator i(section.begin(),section.end(),re),last;i!=last;++i) a.push_back(std::stod(i->str()));
    return a;
}
Policy load_model(const std::string& path) {
    std::ifstream f(path); if(!f)throw std::runtime_error("model missing: "+path+" (run --train first, or use --random/--zero)");
    std::string text((std::istreambuf_iterator<char>(f)),{});
    auto weights=array_from_json(text,"weights"),ls=array_from_json(text,"log_std");
    if(weights.size()!=ACT*OBS||ls.size()!=ACT)throw std::runtime_error("model dimensions incompatible");
    Policy p; std::copy(weights.begin(),weights.end(),p.p.begin()); std::copy(ls.begin(),ls.end(),p.p.begin()+ACT*OBS);
    for(double x:p.p)if(!std::isfinite(x))throw std::runtime_error("non-finite model parameter");
    return p;
}
void save_model(const Policy&p,const Value&v,const Options&o,int trained_steps,int episodes) {
    std::ostringstream j; j<<std::setprecision(17);
    j<<"{\n  \"project\": \"windrunner\",\n  \"format\": \"windrunner-linear-gaussian-v1\",\n"
     <<"  \"algorithm\": \"PPO-Clip with GAE and Adam\",\n  \"train_seed\": "<<o.seed<<",\n  \"training_steps\": "<<trained_steps
     <<",\n  \"training_episodes\": "<<episodes<<",\n  \"observation_order\": [\"bias\",\"relative_y\",\"vx_div4\",\"vy_div3\",\"cave_slope\",\"preview_delta_div3\",\"wind_x_div1_5\",\"wind_y_div1_5\",\"progress\",\"time_remaining\"],\n"
     <<"  \"action_order\": [\"ax\",\"ay\"],\n  \"weights\": [";
    for(int a=0;a<ACT;++a) { if(a)j<<','; j<<'['; for(int k=0;k<OBS;++k){if(k)j<<',';j<<p.p[a*OBS+k];}j<<']'; }
    j<<"],\n  \"log_std\": ["<<p.p[ACT*OBS]<<','<<p.p[ACT*OBS+1]<<"],\n  \"value_weights\": [";
    for(int k=0;k<VF;++k){if(k)j<<',';j<<v.p[k];} j<<"],\n  \"evaluation_policy\": \"deterministic Gaussian mean, clipped to [-1,1]\"\n}";
    write(o.model,j.str());
}
struct Episode { uint32_t seed; double ret,distance; int steps; std::string terminal; };
Action choose(const Policy&p,const Environment&e,const std::string& mode,std::mt19937_64&rng) {
    if(mode=="zero")return {0,0};
    if(mode=="random") { std::uniform_real_distribution<double> uniform(-1,1); return {uniform(rng),uniform(rng)}; }
    return p.act(e.observe());
}
Episode rollout(const Policy&p,uint32_t seed,const std::string& mode) {
    Environment e(seed); std::mt19937_64 rng(uint64_t(seed)^0xd1b54a32d192ed03ull);
    while(!e.done)e.step(choose(p,e,mode,rng));
    return {seed,e.episode_return,e.s.x,e.s.step,e.terminal};
}
std::string metrics(const std::vector<Episode>& episodes) {
    double mean=0,dist=0,steps=0; int goals=0,collisions=0,timeouts=0;
    for(auto&e:episodes){mean+=e.ret;dist+=e.distance;steps+=e.steps;goals+=e.terminal=="goal";collisions+=e.terminal=="collision";timeouts+=e.terminal=="timeout";}
    int n=int(episodes.size());mean/=n; double var=0;for(auto&e:episodes)var+=(e.ret-mean)*(e.ret-mean);var/=n;
    std::ostringstream j;j<<std::setprecision(12)<<"{\"episodes\":"<<n<<",\"meanReturn\":"<<mean<<",\"stdReturn\":"<<std::sqrt(var)
    <<",\"meanDistance\":"<<dist/n<<",\"meanSteps\":"<<steps/n<<",\"successRate\":"<<double(goals)/n<<",\"collisionRate\":"<<double(collisions)/n
    <<",\"timeoutRate\":"<<double(timeouts)/n<<",\"successes\":"<<goals<<",\"collisions\":"<<collisions<<",\"timeouts\":"<<timeouts<<"}";return j.str();
}
uint32_t heldout_seed(uint32_t index) {
    // Avalanche the index so sequential episode numbers do not produce correlated first phases.
    uint32_t x=index+20261005u;
    x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; x^=x>>16;
    return 0x80000000u|(x&0x7fffffffu);
}
std::string evaluate(const Policy&p,int n) {
    std::ostringstream j;j<<std::setprecision(12);
    j<<"{\"project\":\"windrunner\",\"seed\":2147483648,\"mode\":\"evaluate\",\"summary\":{\"protocol\":\"paired held-out cave seeds; deterministic policy mean; random uniform actions; fixed avalanche-hashed seed schedule\","
      "\"heldOutSeedRange\":[2147483648,4294967295],\"heldOutEpisodes\":"<<n<<",\"trainingSeedRange\":[1,1073741823],\"baselines\":{";
    std::vector<std::vector<Episode>> results; std::vector<std::string> modes={"trained","random","zero"};
    for(int m=0;m<3;++m) {
        if(m)j<<',';
        std::vector<Episode> es;
        for(int i=0;i<n;++i)es.push_back(rollout(p,heldout_seed(uint32_t(i)),modes[m]));
        j<<'"'<<modes[m]<<"\":"<<metrics(es);results.push_back(es);
    }
    j<<"}},\"episodes\":[";
    for(int i=0;i<n;++i){if(i)j<<',';j<<"{\"seed\":"<<results[0][i].seed;
        for(int m=0;m<3;++m){auto&e=results[m][i];j<<",\""<<modes[m]<<"\":{\"return\":"<<e.ret<<",\"distance\":"<<e.distance<<",\"steps\":"<<e.steps<<",\"terminal\":\""<<e.terminal<<"\"}";}j<<'}';}
    j<<"],\"frames\":[]}";return j.str();
}
std::string trajectory(const Policy&p,const Options&o) {
    Environment e(o.seed); std::mt19937_64 rng(uint64_t(o.seed)^0xd1b54a32d192ed03ull);std::string mode=o.random?"random":o.zero?"zero":"trained";
    std::vector<std::string> frames{frame_json(e,{0,0},0)};
    while(!e.done&&e.s.step<o.steps){Action a=choose(p,e,mode,rng);auto r=e.step(a);frames.push_back(frame_json(e,a,r.reward));}
    std::ostringstream j;j<<std::setprecision(12)<<"{\"project\":\"windrunner\",\"seed\":"<<o.seed<<",\"mode\":\""<<mode<<"\",\"summary\":{\"return\":"<<e.episode_return
     <<",\"steps\":"<<e.s.step<<",\"distance\":"<<e.s.x<<",\"progress\":"<<clip(e.s.x/GOAL,0,1)<<",\"success\":"<<(e.terminal=="goal"?"true":"false")
     <<",\"collision\":"<<(e.terminal=="collision"?"true":"false")<<",\"terminal\":\""<<e.terminal<<"\"},\"environment\":{\"dt\":0.08,\"goal\":64,\"radius\":0.22,\"horizon\":400,\"phases\":[";
    for(int k=0;k<4;++k){if(k)j<<',';j<<e.phases[k];}j<<"]},\"frames\":[";
    for(size_t i=0;i<frames.size();++i){if(i)j<<',';j<<frames[i];}j<<"]}";return j.str();
}
Policy train(Options &o) {
    Policy p;Value value; Adam<PARAMS> actor;Adam<VF> critic;
    std::mt19937_64 rng(o.seed); auto next_seed=[&](){return uint32_t(rng()%0x3fffffffull)+1;};
    Environment env(next_seed());
    std::filesystem::create_directories(o.out);std::ofstream curve(o.out+"/training_curve.csv");
    curve<<"update,environment_steps,completed_episodes,mean_episode_return,success_rate,mean_distance,policy_kl,clip_fraction,policy_std_x,policy_std_y\n";
    int total=0,all_episodes=0,update=0;constexpr int batch_size=4096,minibatch=256,epochs=8;
    while(total<o.steps) {
        ++update;std::vector<Sample> batch;int count=std::min(batch_size,o.steps-total);batch.reserve(count);
        double episode_returns=0,distance_sum=0;int episodes=0,successes=0;
        for(int i=0;i<count;++i){
            Sample s;s.obs=env.observe();s.value=value.predict(s.obs);s.raw=p.sample(s.obs,rng);s.old_logp=p.log_probability(s.obs,s.raw);
            auto tr=env.step(s.raw);s.reward=tr.reward;s.ended=env.done;
            // A timeout is truncation: bootstrap its value, but never carry GAE into the reset episode.
            s.next_value=tr.terminated?0.:value.predict(env.observe());batch.push_back(s);++total;
            if(env.done){++episodes;++all_episodes;episode_returns+=env.episode_return;distance_sum+=env.s.x;successes+=env.terminal=="goal";env.reset(next_seed());}
        }
        compute_gae(batch);double am=0;for(auto&s:batch)am+=s.adv;am/=batch.size();double av=0;for(auto&s:batch)av+=(s.adv-am)*(s.adv-am);av=std::sqrt(av/batch.size()+1e-8);
        for(auto&s:batch)s.adv=(s.adv-am)/av;
        std::vector<int> order(batch.size());std::iota(order.begin(),order.end(),0);
        double final_kl=0,clipfrac=0;
        for(int epoch=0;epoch<epochs;++epoch){
            std::shuffle(order.begin(),order.end(),rng);
            for(int start=0;start<count;start+=minibatch){
                Parameters pg{};std::array<double,VF> vg{};int n=std::min(minibatch,count-start);
                for(int k=start;k<start+n;++k){auto&s=batch[order[k]];double lr=p.log_probability(s.obs,s.raw)-s.old_logp;
                    double scale=surrogate_log_gradient(lr,s.adv);auto score=p.score(s.obs,s.raw);for(int j=0;j<PARAMS;++j)pg[j]+=scale*score[j]/n;
                    auto f=value_features(s.obs);double error=clip(s.target-value.predict(s.obs),-40,40);for(int j=0;j<VF;++j)vg[j]+=error*f[j]/n;
                }
                // Gaussian entropy H = sum(log_std + const); its derivative is one.
                for(int a=0;a<ACT;++a)pg[ACT*OBS+a]+=.002;
                actor.update(p.p,pg,.0025,.8);critic.update(value.p,vg,.012,10);
                for(int a=0;a<ACT;++a)p.p[ACT*OBS+a]=clip(p.p[ACT*OBS+a],-2.4,.3);
            }
            final_kl=0;clipfrac=0;
            for(auto&s:batch){double lr=p.log_probability(s.obs,s.raw)-s.old_logp;double r=std::exp(clip(lr,-20,20));final_kl+=(r-1)-lr;clipfrac+=std::abs(r-1)>.2;}
            final_kl/=count;clipfrac/=count;
            if(final_kl>.025)break;
        }
        double mean=episodes?episode_returns/episodes:0,rate=episodes?double(successes)/episodes:0;
        curve<<std::setprecision(12)<<update<<','<<total<<','<<episodes<<','<<mean<<','<<rate<<','<<(episodes?distance_sum/episodes:0)<<','<<final_kl<<','<<clipfrac<<','<<std::exp(p.p[ACT*OBS])<<','<<std::exp(p.p[ACT*OBS+1])<<'\n';
        if(update%10==0||total==o.steps)std::cerr<<"update="<<update<<" steps="<<total<<" train_return="<<mean<<" train_success="<<rate<<" kl="<<final_kl<<'\n';
    }
    save_model(p,value,o,total,all_episodes);
    write(o.out+"/evaluation.json",evaluate(p,o.episodes));
    Options trace=o;trace.seed=0x8000000bu;trace.steps=400;write(o.out+"/trajectory.json",trajectory(p,trace));
    std::ostringstream j;j<<"{\"project\":\"windrunner\",\"seed\":"<<o.seed<<",\"mode\":\"train\",\"summary\":{\"trainingSteps\":"<<total<<",\"updates\":"<<update<<",\"episodes\":"<<all_episodes<<",\"model\":\""<<o.model<<"\"},\"frames\":[]}";
    if(o.json)std::cout<<j.str()<<'\n';else std::cout<<"Trained "<<total<<" environment steps. Saved model, curve, held-out evaluation, and trajectory to "<<o.out<<"\n";
    return p;
}
}
int main(int argc,char**argv) {
    try {
        Options o=parse(argc,argv);
        if(o.train){if(o.model=="artifacts/model.json")o.model=o.out+"/model.json";train(o);return 0;}
        Policy p;if(!o.random&&!o.zero)p=load_model(o.model);
        std::string result=o.evaluate?evaluate(p,o.episodes):trajectory(p,o);
        if(o.evaluate)write(o.out+"/evaluation.json",result);
        if(o.json)std::cout<<result<<'\n';
        else if(o.evaluate)std::cout<<result<<'\n';
        else std::cout<<"Trajectory generated for seed "<<o.seed<<"; use --json for the full record.\n";
        return 0;
    }catch(const std::exception&e){std::cerr<<"windrunner: "<<e.what()<<'\n';return 1;}
}
