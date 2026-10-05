#include "core.hpp"
#include <iostream>
#include <stdexcept>
int main(){int checks=0;try{
 auto check=[&](bool pass,const std::string& name){checks++;if(!pass)throw std::runtime_error(name);};
 // Observation equality proves changing every other role cannot alter a private initial view.
 std::array<bool,6> base{false,true,true,false,false,false};veil::Engine original(91,base);
 for(int a=1;a<6;a++)for(int b=a+1;b<6;b++){std::array<bool,6> alt{};alt[a]=alt[b]=true;veil::Engine changed(91,alt);check(original.observationJson(0)==changed.observationJson(0),"private-role noninterference");}
 veil::Engine privateChat(91,base);std::string err;
 check(privateChat.apply({0,0,"message",1,"private assessment"},err),"valid private message");
 check(privateChat.observationJson(1).find("private assessment")!=std::string::npos,"recipient sees message");
 check(privateChat.observationJson(2).find("private assessment")==std::string::npos,"unrelated agent cannot read private message");
 check(privateChat.frameJson("test").find("private assessment")==std::string::npos,"public frame excludes private messages");
 auto before=privateChat.observationJson(0);
 check(!privateChat.apply({0,0,"vote",0,""},err),"self vote rejected");check(before==privateChat.observationJson(0),"invalid action preserves state");
 check(!privateChat.apply({0,1,"message",-1,"future"},err),"future action rejected");
 check(!privateChat.apply({0,0,"message",-1,std::string(181,'x')},err),"oversized message rejected");
 privateChat.setPhase("ballot");check(privateChat.apply({0,0,"vote",1,""},err),"valid vote");check(!privateChat.apply({0,0,"vote",2,""},err),"duplicate vote rejected");
 check(!privateChat.apply({0,0,"message",-1,"late"},err),"wrong phase rejected");
 privateChat.setPhase("night");check(!privateChat.apply({0,0,"kill",1,""},err),"councilor cannot assassinate");
 check(privateChat.apply({1,0,"kill",3,""},err),"traitor can use permitted night action");check(!privateChat.apply({3,0,"kill",1,""},err),"eliminated agent blocked");
 auto parsed=veil::parseAction("{\"agent\":0,\"round\":0,\"type\":\"message\",\"target\":-1,\"text\":\"line\\nquote\\\"\"}");check(parsed.text=="line\nquote\"","JSON escape decoding");
 for(const auto& text:std::vector<std::string>{"{}","{\"agent\":0,\"type\":\"vote\",\"target\":1,\"role\":\"traitor\"}","{\"agent\":0.5,\"type\":\"vote\"}","{\"agent\":0,\"agent\":1,\"type\":\"vote\"}","{\"agent\":0,\"type\":\"vote\"} trailing"}){bool rejected=false;try{veil::parseAction(text);}catch(...){rejected=true;}check(rejected,"malformed action rejected");}
 veil::Engine interactive(42);veil::AgentMemory policyMemory(19);int supplied=0;
 interactive.runRound({},[&](const veil::View& view,const std::string& type)->std::optional<veil::Action>{
  if(view.id!=0)return std::nullopt;
  check(view.id==0&&view.ownRole==interactive.observe(0).ownRole,"external provider private identity");supplied++;return veil::heuristic(view,policyMemory,type);
 });check(supplied==3,"external provider receives talk offer ballot boundaries");check(interactive.invariant(),"external provider transition invariant");
 for(unsigned seed=0;seed<100;seed++){
  veil::Engine a(seed),b(seed);
  for(int round=0;round<12;round++){
   a.runRound();b.runRound();check(a.invariant(),"state invariant");check(a.trajectoryJson()==b.trajectoryJson(),"seeded trajectory repeatability");
   for(int id=0;id<6;id++){auto v=minijson::parse(a.observationJson(id));check(v.at("agent").integer()==id,"observation identity");check(!v.has("roles")&&!v.has("otherRoles"),"no hidden-role arrays");}
  }
  auto trajectory=minijson::parse(a.trajectoryJson());check(trajectory.at("frames").kind==minijson::Value::Array,"valid trajectory JSON");check(trajectory.at("summary").at("alive").integer()>=1,"living delegate range");
 }
 std::cout<<"{\"project\":\"veil\",\"passed\":true,\"checks\":"<<checks<<",\"seeds\":100,\"roundCallsPerSeed\":12,\"suites\":[\"private-role noninterference\",\"private message isolation\",\"action validation and rollback\",\"JSON boundary\",\"deterministic trajectories\",\"state invariants\"]}\n";
 }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<" checks: "<<e.what()<<'\n';return 1;}return 0;}
