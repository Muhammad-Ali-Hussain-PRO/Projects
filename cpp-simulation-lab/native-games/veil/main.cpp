#include "core.hpp"
#include <iostream>
#include <fstream>
#include <limits>
#include <set>
#include <tuple>
int main(int argc,char** argv){
 try{
  unsigned seed=42;int steps=12,observation=-1,externalAgent=-1;bool json=false;std::string action,actionsPath,phase="talk";
  for(int i=1;i<argc;i++){
   std::string a=argv[i];auto arg=[&](){if(++i>=argc)throw std::runtime_error("missing argument for "+a);return std::string(argv[i]);};
   if(a=="--seed"){auto value=arg();size_t used=0;auto n=std::stoull(value,&used);if(value.empty()||value.front()=='-'||used!=value.size()||n>std::numeric_limits<unsigned>::max())throw std::runtime_error("seed must be an unsigned 32-bit integer");seed=static_cast<unsigned>(n);}
   else if(a=="--steps"){auto value=arg();size_t used=0;steps=std::stoi(value,&used);if(used!=value.size())throw std::runtime_error("invalid steps");}
   else if(a=="--json")json=true;
   else if(a=="--external-agent")externalAgent=std::stoi(arg());else if(a=="--observation")observation=std::stoi(arg());else if(a=="--action")action=arg();else if(a=="--actions")actionsPath=arg();else if(a=="--phase")phase=arg();
   else if(a=="--help"){std::cout<<"Veil Council (C++17)\n--seed N --steps N --json\n--observation ID [--phase talk|ballot|night] [--action JSON]\n--actions FILE (JSONL round overrides)\n--external-agent ID (private JSONL request/action protocol)\nSix independent heuristic policies; no model/provider connection.\n";return 0;}else throw std::runtime_error("unknown option: "+a);
  }
  if(externalAgent< -1||externalAgent>=6)throw std::runtime_error("external agent must be 0..5");
  if(steps<0||steps>10000)throw std::runtime_error("steps must be 0..10000");
  veil::Engine engine(seed);engine.setPhase(phase);
  if(!action.empty()){
   auto a=veil::parseAction(action);std::string error;bool ok=engine.apply(a,error);
   std::cout<<"{\"accepted\":"<<(ok?"true":"false")<<",\"error\":"<<minijson::quote(error)<<",\"observation\":"<<(a.agent>=0&&a.agent<6?engine.observationJson(a.agent):"null")<<"}\n";return ok?0:2;
  }
  if(observation>=0){std::cout<<engine.observationJson(observation)<<'\n';return 0;}
  std::vector<veil::Action> external;std::set<std::tuple<int,int,std::string>> overrideKeys;
  if(!actionsPath.empty()){
   std::ifstream f(actionsPath);if(!f)throw std::runtime_error("cannot open actions file");std::string line;
   while(std::getline(f,line))if(!line.empty()){
    auto a=veil::parseAction(line);
    if(a.agent<0||a.agent>=6||a.round<1||a.round>steps)throw std::runtime_error("override agent/round out of range");
    if(a.type!="message"&&a.type!="offer"&&a.type!="vote"&&a.type!="kill")throw std::runtime_error("unsupported override action type");
    if(!overrideKeys.emplace(a.round,a.agent,a.type).second)throw std::runtime_error("duplicate round/agent/type override");
    if(a.type=="message"&&(a.text.empty()||a.text.size()>180||a.target< -1||a.target>=6))throw std::runtime_error("invalid message override");
    if(a.type!="message"&&(a.target<0||a.target>=6||a.target==a.agent))throw std::runtime_error("invalid override target");
    if(a.type=="kill"&&a.round%3!=0)throw std::runtime_error("kill override requires a sabotage round");
    external.push_back(a);
   }
  }
  auto provider=[&](const veil::View& v,const std::string& requested)->std::optional<veil::Action>{
   if(v.id!=externalAgent)return std::nullopt;
   std::cout<<"{\"kind\":\"request\",\"requestedType\":"<<minijson::quote(requested)<<",\"observation\":"<<engine.observationJson(v.id)<<"}\n"<<std::flush;
   std::string line;if(!std::getline(std::cin,line))throw std::runtime_error("external agent input ended before action");
   return veil::parseAction(line);
  };
  for(int i=0;i<steps&&engine.winner()=="ongoing";i++)engine.runRound(external,provider);
  if(json)std::cout<<engine.trajectoryJson()<<'\n';else std::cout<<"Veil Council | seed "<<seed<<" | "<<engine.round()<<" rounds | "<<engine.aliveCount()<<" living delegates | result: "<<engine.winner()<<"\nPolicies are seeded independent heuristics. Use --json for public frames; --observation ID for a private view.\n";
 }catch(const std::exception& e){std::cerr<<"veil: "<<e.what()<<'\n';return 1;}return 0;
}
