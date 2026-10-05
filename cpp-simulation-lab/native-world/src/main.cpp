#include "aether/world.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
unsigned integer(const std::string& value,unsigned maximum,const std::string& name) {
  if(value.empty()||value.size()>10||value.find_first_not_of("0123456789")!=std::string::npos)
    throw std::invalid_argument(name+" requires an unsigned decimal integer");
  const auto n=std::stoull(value);
  if(n>maximum) throw std::invalid_argument(name+" exceeds supported maximum");
  return static_cast<unsigned>(n);
}
}
int main(int argc,char** argv) {
  try {
    unsigned seed=42,steps=1200,sample_every=30;
    bool json=false;
    aether::Spec spec;
    std::vector<aether::Action> actions;
    for(int i=1;i<argc;++i) {
      const std::string arg=argv[i];
      if(arg=="--help") {
        std::cout<<"Aether World — deterministic C++17 procedural sandbox\n"
          "Usage: aether [--seed UINT32] [--steps 0..100000] [--sample-every 1..100000] [--json]\n"
          "              [--prompt 'floating island; platforms 4; bridges on; water puzzle; bodies 3']\n"
          "              [--valve feed|gate=on|off] [--action STEP:feed|gate=on|off]\n"
          "Prompt clauses: island, platforms INT, bridges on|off, water on|off|puzzle, bodies INT, gravity NUMBER, wind NUMBER.\n"
          "Spec bounds repaired deterministically: platforms/bodies 1..8, gravity 0.5..20, wind -5..5.\n"
          "Actions occur before that numbered simulation step. JSON includes initial/final frames.\n";
        return 0;
      }
      if(arg=="--json") {json=true;continue;}
      if(i+1>=argc) throw std::invalid_argument("missing argument after "+arg);
      const std::string value=argv[++i];
      if(arg=="--seed") seed=integer(value,0xffffffffu,"seed");
      else if(arg=="--steps") steps=integer(value,100000,"steps");
      else if(arg=="--sample-every") {sample_every=integer(value,100000,"sample-every");if(!sample_every)throw std::invalid_argument("sample-every must be positive");}
      else if(arg=="--prompt") spec=aether::parse_prompt(value);
      else if(arg=="--valve") actions.push_back(aether::parse_action("0:"+value));
      else if(arg=="--action") actions.push_back(aether::parse_action(value));
      else throw std::invalid_argument("unknown option: "+arg);
    }
    std::stable_sort(actions.begin(),actions.end(),[](const auto& a,const auto& b){return a.step<b.step;});
    for(const auto& action:actions)if(action.step>=steps&&action.step!=0)throw std::invalid_argument("action must occur before final step");
    aether::World world(spec,seed);
    std::size_t next=0;
    while(next<actions.size()&&actions[next].step==0){world.set_valve(actions[next].valve,actions[next].open);++next;}
    std::vector<aether::Frame> frames{world.frame()};
    for(unsigned i=0;i<steps;++i) {
      while(next<actions.size()&&actions[next].step==i){world.set_valve(actions[next].valve,actions[next].open);++next;}
      world.step();
      if((i+1)%sample_every==0||i+1==steps)frames.push_back(world.frame());
    }
    if(json) std::cout<<world.json(frames,steps);
    else {
      const auto final=world.frame();const auto validation=world.validate();
      std::cout<<"Aether World | seed "<<seed<<" | "<<steps<<" fixed steps at 120 Hz\n"
        <<world.geometry().size()<<" generated meshes, "<<world.bodies().size()<<" rigid AABB bodies\n"
        <<"Water: "<<final.total_volume<<" / initial "<<world.initial_volume()<<" volume units\n"
        <<"Puzzle: "<<(final.puzzle_solved?"solved":"waiting")<<" | constraints: "<<(validation.ok?"valid":"failed")
        <<" ("<<validation.checks<<" checks, "<<validation.repairs.size()<<" repairs)\n";
    }
    return world.validate().ok?0:3;
  } catch(const std::exception& e) {std::cerr<<"aether: "<<e.what()<<'\n';return 2;}
}
