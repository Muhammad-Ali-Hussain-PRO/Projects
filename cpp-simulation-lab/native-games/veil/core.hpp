#pragma once
#include "json.hpp"
#include <array>
#include <algorithm>
#include <random>
#include <sstream>
#include <iomanip>
#include <numeric>
#include <optional>
#include <functional>
namespace veil {
constexpr int Agents=6;
struct Message{int from,to;std::string text;};
struct Vote{int from,target;};
struct Offer{int from,to,target;bool accepted;};
struct Action{int agent=0,round=0;std::string type;int target=-1;std::string text;};
struct View{
 int id,round;std::string ownRole,phase;std::array<bool,Agents> alive;std::array<double,Agents> suspicion;
 std::vector<Message> messages;std::vector<Vote> votes;std::vector<Offer> offers;
};
struct AgentMemory{
 std::array<double,Agents> suspicion{};std::mt19937 random;
 explicit AgentMemory(unsigned seed=1):random(seed){suspicion.fill(1.0/3.0);}
};
inline int chooseTarget(const View& v,AgentMemory& memory){
 double best=-1e9;int target=-1;
 for(int j=0;j<Agents;j++)if(v.alive[j]&&j!=v.id){
  double jitter=double(memory.random()%101)/10000.0;
  double score=(v.ownRole=="traitor"?1.0-v.suspicion[j]:v.suspicion[j])+jitter;
  if(score>best){best=score;target=j;}
 }
 return target;
}
inline Action heuristic(const View& v,AgentMemory& memory,const std::string& type){
 Action a;a.agent=v.id;a.round=v.round;a.type=type;a.target=chooseTarget(v,memory);
 if(type=="message"){a.text="I propose examining delegate "+std::to_string(a.target)+"; voting patterns should settle this.";a.target=-1;}
 if(type=="vote")for(const auto& o:v.offers)if(o.to==v.id&&o.accepted&&v.alive[o.target]&&o.target!=v.id){a.target=o.target;break;}
 return a;
}
inline Action parseAction(const std::string& text){
 auto v=minijson::parse(text);if(v.kind!=minijson::Value::Object)throw std::runtime_error("action must be object");
 for(const auto& kv:v.o)if(kv.first!="agent"&&kv.first!="round"&&kv.first!="type"&&kv.first!="target"&&kv.first!="text")throw std::runtime_error("unknown action field: "+kv.first);
 Action a;a.agent=v.at("agent").integer();a.type=v.at("type").string();
 if(v.has("round")) a.round=v.at("round").integer();
 if(v.has("target")) a.target=v.at("target").integer();
 if(v.has("text")) a.text=v.at("text").string();
 return a;
}
class Engine{
 unsigned seed_;int round_=0;std::string phase_="talk";std::array<bool,Agents> alive_{};std::array<bool,Agents> traitor_{};
 std::array<AgentMemory,Agents> memory_;std::vector<Message> messages_;std::vector<Vote> votes_;std::vector<Offer> offers_;std::vector<int> eliminated_;std::vector<std::string> frames_;
 std::string result_="ongoing";
 static double bounded(double p){return std::max(0.05,std::min(0.95,p));}
 void determineWinner(){int evil=0,good=0;for(int i=0;i<Agents;i++)if(alive_[i]){if(traitor_[i])evil++;else good++;}if(evil==0)result_="council";else if(evil>=good)result_="traitors";}
 void updateBeliefs(){
  std::array<int,Agents> tally{};for(auto& v:votes_)tally[v.target]++;
  for(int i=0;i<Agents;i++)if(alive_[i])for(int j=0;j<Agents;j++)if(i!=j&&alive_[j]){
   double change=0;for(auto& v:votes_)if(v.from==j){change+=(tally[v.target]<=1?0.09:-0.04);if(v.target==i)change+=0.12;}
   memory_[i].suspicion[j]=bounded(memory_[i].suspicion[j]+change);
  }
 }
 void snapshot(const std::string& text){frames_.push_back(frameJson(text));}
 public:
 explicit Engine(unsigned seed,std::optional<std::array<bool,Agents>> roles=std::nullopt):seed_(seed){
  alive_.fill(true);std::array<int,Agents> order{0,1,2,3,4,5};std::mt19937 random(seed);std::shuffle(order.begin(),order.end(),random);
  if(roles)traitor_=*roles;else{traitor_[order[0]]=true;traitor_[order[1]]=true;}
  for(int i=0;i<Agents;i++){memory_[i]=AgentMemory(seed*37u+static_cast<unsigned>(i)*7919u+17u);for(int j=0;j<Agents;j++)memory_[i].suspicion[j]=i==j?0.0:0.23+double(memory_[i].random()%210)/1000.0;}
  snapshot("Six delegates enter the council. Roles stay private; agents use seeded heuristic policies.");
 }
 unsigned seed()const{return seed_;}int round()const{return round_;}const std::string& winner()const{return result_;}int aliveCount()const{return std::count(alive_.begin(),alive_.end(),true);}
 View observe(int id)const{
  if(id<0||id>=Agents)throw std::runtime_error("agent must be 0..5");
  View v{id,round_,traitor_[id]?"traitor":"councilor",phase_,alive_,memory_[id].suspicion,{},{},{}};
  for(auto& m:messages_)if(m.to==-1||m.to==id||m.from==id)v.messages.push_back(m);
  v.votes=votes_;for(auto& o:offers_)if(o.from==id||o.to==id)v.offers.push_back(o);return v;
 }
 std::string validate(const Action& a)const{
  if(a.agent<0||a.agent>=Agents)return "agent must be 0..5";
  if(!alive_[a.agent])return "eliminated agent cannot act";
  if(result_!="ongoing")return "game already ended";
  if(a.round!=round_)return "stale or future round";
  if(a.type!="message"&&a.type!="offer"&&a.type!="vote"&&a.type!="kill")return "unsupported action type";
  if(a.type=="message"){
   if(phase_!="talk")return "message requires talk phase";
   if(a.target< -1||a.target>=Agents||(a.target>=0&&!alive_[a.target]))return "invalid recipient";
   if(a.text.empty()||a.text.size()>180)return "message length must be 1..180 bytes";
  }else{
   if(a.target<0||a.target>=Agents||!alive_[a.target]||a.target==a.agent)return "target must be another living agent";
   if(a.type=="offer"&&phase_!="talk")return "offer requires talk phase";
   if(a.type=="vote"&&phase_!="ballot")return "vote requires ballot phase";
   if(a.type=="kill"&&(phase_!="night"||!traitor_[a.agent]))return "kill requires night and own traitor role";
   if(a.type=="vote")for(auto& v:votes_)if(v.from==a.agent)return "one vote per round";
  }
  return "";
 }
 bool apply(const Action& a,std::string& error){
  error=validate(a);if(!error.empty())return false;
  if(a.type=="message")messages_.push_back({a.agent,a.target,a.text});
  else if(a.type=="vote")votes_.push_back({a.agent,a.target});
  else if(a.type=="offer"){
   int recipient=-1;double trust=2;
   for(int j=0;j<Agents;j++)if(alive_[j]&&j!=a.agent&&j!=a.target){double p=memory_[a.agent].suspicion[j];if(p<trust){trust=p;recipient=j;}}
   if(recipient<0){error="no eligible coalition recipient";return false;}
   auto view=observe(recipient);bool accept=view.suspicion[a.agent]<0.46&&view.suspicion[a.target]>0.20;
   offers_.push_back({a.agent,recipient,a.target,accept});
  }else{alive_[a.target]=false;eliminated_.push_back(a.target);determineWinner();}
  return true;
 }
 void setPhase(const std::string& phase){if(phase!="talk"&&phase!="ballot"&&phase!="night")throw std::runtime_error("invalid phase");phase_=phase;}
 void runRound(const std::vector<Action>& external={},
               const std::function<std::optional<Action>(const View&,const std::string&)>& provider={}){
  if(result_!="ongoing") return;
  ++round_;phase_="talk";messages_.clear();votes_.clear();offers_.clear();eliminated_.clear();
  auto run=[&](int i,const std::string& type){
   auto a=heuristic(observe(i),memory_[i],type);
   for(const auto& override:external)if(override.round==round_&&override.agent==i&&override.type==type){a=override;break;}
   if(provider){auto supplied=provider(observe(i),type);if(supplied){a=*supplied;if(a.agent!=i||a.type!=type||a.round!=round_)throw std::runtime_error("external action does not match requested agent/type/round");}}
   std::string e;if(!apply(a,e))throw std::runtime_error("invalid round action: "+e);
  };
  for(int i=0;i<Agents;i++)if(alive_[i])run(i,"message");
  for(int i=0;i<Agents;i++)if(alive_[i])run(i,"offer");
  snapshot("Delegates publish accusations and negotiate public coalition proposals.");phase_="ballot";
  for(int i=0;i<Agents;i++)if(alive_[i])run(i,"vote");
  std::array<int,Agents> tally{};for(auto& v:votes_)tally[v.target]++;
  int accused=int(std::max_element(tally.begin(),tally.end())-tally.begin());
  if(tally[accused]>aliveCount()/2){alive_[accused]=false;eliminated_.push_back(accused);}updateBeliefs();determineWinner();
  snapshot(eliminated_.empty()?"No strict majority: the council deadlocks and suspicion shifts.":"The council eliminates a delegate; their role remains sealed.");
  if(result_=="ongoing"&&round_%3==0){phase_="night";
   for(int i=0;i<Agents;i++)if(alive_[i]&&traitor_[i]){run(i,"kill");break;}
   snapshot("A third-round sabotage removes one delegate. The actor remains hidden.");
  }
 }
 std::string observationJson(int id)const{
  auto v=observe(id);std::ostringstream o;o<<"{\"agent\":"<<v.id<<",\"round\":"<<v.round<<",\"ownRole\":"<<minijson::quote(v.ownRole)<<",\"phase\":"<<minijson::quote(v.phase)<<",\"alive\":[";
  bool first=true;for(int j=0;j<Agents;j++)if(v.alive[j]){if(!first)o<<',';first=false;o<<j;}o<<"],\"suspects\":[";first=true;
  for(int j=0;j<Agents;j++)if(v.alive[j]&&j!=id){if(!first)o<<',';first=false;o<<"{\"id\":"<<j<<",\"p\":"<<std::fixed<<std::setprecision(3)<<v.suspicion[j]<<'}';}
  o<<"],\"messages\":[";first=true;for(auto& m:v.messages){if(!first)o<<',';first=false;o<<"{\"from\":"<<m.from<<",\"to\":"<<m.to<<",\"text\":"<<minijson::quote(m.text)<<'}';}
  o<<"],\"votes\":[";first=true;for(auto& x:v.votes){if(!first)o<<',';first=false;o<<"{\"from\":"<<x.from<<",\"target\":"<<x.target<<'}';}
  o<<"],\"offers\":[";first=true;for(auto& x:v.offers){if(!first)o<<',';first=false;o<<"{\"from\":"<<x.from<<",\"to\":"<<x.to<<",\"target\":"<<x.target<<",\"accepted\":"<<(x.accepted?"true":"false")<<'}';}o<<"]}";return o.str();
 }
 std::string frameJson(const std::string& text)const{
  std::ostringstream o;o<<"{\"round\":"<<round_<<",\"phase\":"<<minijson::quote(phase_)<<",\"alive\":[";bool first=true;
  for(int i=0;i<Agents;i++)if(alive_[i]){if(!first)o<<',';first=false;o<<i;}
  o<<"],\"beliefs\":[";first=true;
  for(int i=0;i<Agents;i++)if(alive_[i]){if(!first)o<<',';first=false;o<<"{\"id\":"<<i<<",\"suspects\":[";bool f=true;for(int j=0;j<Agents;j++)if(alive_[j]&&j!=i){if(!f)o<<',';f=false;o<<"{\"id\":"<<j<<",\"p\":"<<std::fixed<<std::setprecision(3)<<memory_[i].suspicion[j]<<'}';}o<<"]}";}
  o<<"],\"messages\":[";first=true;for(auto& m:messages_)if(m.to==-1){if(!first)o<<',';first=false;o<<"{\"from\":"<<m.from<<",\"to\":"<<m.to<<",\"text\":"<<minijson::quote(m.text)<<'}';}
  o<<"],\"votes\":[";first=true;for(auto& v:votes_){if(!first)o<<',';first=false;o<<"{\"from\":"<<v.from<<",\"target\":"<<v.target<<'}';}
  o<<"],\"offers\":[";first=true;for(auto& x:offers_){if(!first)o<<',';first=false;o<<"{\"from\":"<<x.from<<",\"to\":"<<x.to<<",\"target\":"<<x.target<<",\"accepted\":"<<(x.accepted?"true":"false")<<'}';}
  o<<"],\"eliminated\":[";for(size_t i=0;i<eliminated_.size();i++){if(i)o<<',';o<<eliminated_[i];}o<<"],\"text\":"<<minijson::quote(text)<<'}';return o.str();
 }
 std::string trajectoryJson()const{
  std::ostringstream o;o<<"{\"project\":\"Veil Council\",\"seed\":"<<seed_<<",\"mode\":\"native heuristic simulation\",\"summary\":{\"winner\":"<<minijson::quote(result_)<<",\"rounds\":"<<round_<<",\"alive\":"<<aliveCount()<<"},\"frames\":[";
  for(size_t i=0;i<frames_.size();i++){if(i)o<<',';o<<frames_[i];}o<<"]}";return o.str();
 }
 bool invariant()const{for(int i=0;i<Agents;i++)for(double p:memory_[i].suspicion)if(!std::isfinite(p)||p<0||p>1)return false;for(auto& v:votes_)if(v.from<0||v.from>=Agents||v.target<0||v.target>=Agents)return false;return true;}
};
}
