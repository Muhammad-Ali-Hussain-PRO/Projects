#include "aether/world.hpp"
#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
int assertions=0,groups=0;
void require(bool condition,const std::string& message) {
  ++assertions;
  if(!condition)throw std::runtime_error(message);
}
void rejects(const std::function<void()>& run,const std::string& name) {
  bool rejected=false;
  try {run();}catch(const std::invalid_argument&){rejected=true;}
  require(rejected,name);
}
void group(const std::string& name,const std::function<void()>& run) {
  run();++groups;std::cout<<"PASS "<<name<<'\n';
}
}
int main() {
  try {
    group("bounded prompt grammar and action rejection",[]{
      auto s=aether::parse_prompt("Floating Island; platforms 6; bridges off; water puzzle; bodies 5; gravity 12; wind -2");
      require(s.platforms==6&&!s.bridges&&s.water&&s.bodies==5&&s.gravity==12&&s.wind==-2,"parsed clause values");
      rejects([]{aether::parse_prompt("island; run shell");},"unknown clause rejected");
      rejects([]{aether::parse_prompt("island; gravity nan");},"NaN rejected");
      rejects([]{aether::parse_prompt("gravity 0x1p2");},"hexadecimal number syntax rejected");
      rejects([]{aether::parse_prompt("gravity 1e");},"incomplete decimal exponent rejected");
      require(aether::parse_prompt("gravity 9.81e0; wind -.2").gravity==9.81,"decimal/scientific number syntax");
      rejects([]{aether::parse_prompt("platforms 3; platforms 4");},"duplicate clause rejected");
      rejects([]{aether::parse_prompt("bodies -1");},"negative count rejected");
      rejects([]{aether::parse_prompt("island;");},"trailing empty clause rejected");
      rejects([]{aether::parse_prompt(std::string(513,'a'));},"oversized prompt rejected");
      rejects([]{aether::parse_action("100001:gate=on");},"action step limit");
      rejects([]{aether::parse_action("10:missing=on");},"unknown action valve rejected");
      rejects([]{aether::parse_action("10:gate=maybe");},"invalid action state rejected");
      const auto action=aether::parse_action("240:gate=on");
      require(action.step==240&&action.valve=="gate"&&action.open,"action parsed");
    });
    group("seed reproducibility and procedural geometry",[]{
      aether::World a(aether::Spec{},42),b(aether::Spec{},42),c(aether::Spec{},43);
      std::vector<aether::Frame> fa{a.frame()},fb{b.frame()};
      for(int i=0;i<1200;++i){a.step();b.step();}
      fa.push_back(a.frame());fb.push_back(b.frame());
      require(a.json(fa,1200)==b.json(fb,1200),"same seed byte-identical JSON");
      require(a.geometry()[5].position.x!=c.geometry()[5].position.x,"different seed changes procedural placement");
      require(a.geometry().size()==54,"default geometry count");
      require(a.geometry()[2].vertices.size()==27&&a.geometry()[2].indices.size()==24,"faceted island mesh arrays");
      require(a.validate().ok,"generated meshes and scene pass constraints");
    });
    group("bounded deterministic spec and spawn repair",[]{
      auto s=aether::parse_prompt("platforms 999; bodies 999; gravity 1000; wind -100");
      aether::World a(s,9),b(s,9);
      require(a.spec().platforms==8&&a.spec().bodies==8&&a.spec().gravity==20&&a.spec().wind==-5,"repair clamp bounds");
      require(a.validate().repairs.size()>=4&&a.validate().repairs.size()<=32,"repair report bounded");
      require(a.json({a.frame()},0)==b.json({b.frame()},0),"repair deterministic");
      auto crowded=aether::parse_prompt("platforms 1; bodies 8; water off");
      for(unsigned seed=0;seed<16;++seed) {
        aether::World w(crowded,seed);
        require(w.validate().ok,"crowded spawn repaired");
        require(w.validate().repairs.size()<=32,"crowded repair budget");
      }
    });
    group("gravity, floor contact, friction, and penetration bounds",[]{
      auto spec=aether::parse_prompt("island; bodies 1; water off");
      aether::World w(spec,42);
      const auto initial=w.frame();w.step();const auto first=w.frame();
      require(first.bodies[0].position.y<initial.bodies[0].position.y,"gravity changes position");
      require(std::abs(first.bodies[0].velocity.y+spec.gravity*aether::World::dt)<1e-12,"fixed-step acceleration");
      for(int i=0;i<2399;++i) w.step();
      const auto final=w.frame();
      require(final.bodies[0].resting,"body rests on island");
      require(std::abs(final.bodies[0].position.y-1.4)<1e-6,"AABB body rests at top plus half-height");
      require(std::abs(final.bodies[0].velocity.y)<1e-12,"normal contact velocity constrained");
      require(std::abs(final.bodies[0].velocity.x)<1e-5&&std::abs(final.bodies[0].velocity.z)<1e-5,"contact friction dissipates tangential motion");
      require(final.max_penetration<1e-4&&w.validate().ok,"floor does not penetrate");
      aether::World crowded(aether::parse_prompt("platforms 1; bodies 8; water off"),1);
      for(int i=0;i<2400;++i) {
        crowded.step();
        require(crowded.frame().max_penetration<1e-4,"crowded contact bounds at each step");
      }
      require(crowded.validate().ok,"bounded dynamic pair and static contact projection");
    });
    group("conservative reservoir flow and valve puzzle",[]{
      aether::World closed(aether::Spec{},42),open(aether::Spec{},42);
      open.set_valve("gate",true);
      double worst=0;
      for(int i=0;i<10000;++i) {
        closed.step();open.step();
        worst=std::max(worst,std::abs(open.total_volume()-open.initial_volume()));
        for(const auto& r:open.reservoirs())require(r.volume>=-1e-8&&r.volume<=r.capacity+1e-8,"capacity-limited conservative flow");
      }
      require(worst<1e-9,"long-run volume conservation");
      require(closed.reservoirs()[2].volume==0&&!closed.frame().puzzle_solved,"closed gate isolates goal");
      require(open.frame().puzzle_solved&&open.reservoirs()[2].level()>=1,"open gate solves goal threshold");
      const double goal=open.reservoirs()[2].volume;
      open.set_valve("gate",false);
      for(int i=0;i<1000;++i)open.step();
      require(open.reservoirs()[2].volume==goal,"closed valve stops exchange");
      require(open.validate().ok&&closed.validate().ok,"water and physics invariants after long run");
      aether::World dry(aether::parse_prompt("water off"),1);
      rejects([&]{dry.set_valve("gate",true);},"actions cannot reference missing reservoir system");
    });
    std::cout<<"PASS "<<groups<<" groups, "<<assertions<<" assertions\n";
    return 0;
  }catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
