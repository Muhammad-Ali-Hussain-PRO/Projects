#include "aether/world.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace aether {
namespace {
constexpr double eps=1e-8;
double& axis(Vec3& v,int a) { return a==0?v.x:(a==1?v.y:v.z); }
double axis(const Vec3& v,int a) { return a==0?v.x:(a==1?v.y:v.z); }
bool finite(Vec3 v) { return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z); }
bool overlap(Vec3 p,Vec3 s,Vec3 q,Vec3 t) {
  return std::abs(p.x-q.x)<(s.x+t.x)/2-eps &&
         std::abs(p.y-q.y)<(s.y+t.y)/2-eps &&
         std::abs(p.z-q.z)<(s.z+t.z)/2-eps;
}
double penetration(Vec3 p,Vec3 s,Vec3 q,Vec3 t) {
  if(!overlap(p,s,q,t)) return 0;
  return std::min({(s.x+t.x)/2-std::abs(p.x-q.x),
                   (s.y+t.y)/2-std::abs(p.y-q.y),
                   (s.z+t.z)/2-std::abs(p.z-q.z)});
}
std::string trim(std::string s) {
  const auto first=s.find_first_not_of(" \t\r\n");
  if(first==std::string::npos) return "";
  const auto last=s.find_last_not_of(" \t\r\n");
  s=s.substr(first,last-first+1);
  for(char& c:s) if(c>='A'&&c<='Z') c=char(c-'A'+'a');
  return s;
}
double number(const std::string& s) {
  // Restrict std::stod's broader syntax to the documented decimal grammar.
  std::size_t cursor=0,digits=0;
  if(cursor<s.size()&&(s[cursor]=='+'||s[cursor]=='-')) ++cursor;
  while(cursor<s.size()&&s[cursor]>='0'&&s[cursor]<='9'){++cursor;++digits;}
  if(cursor<s.size()&&s[cursor]=='.') {
    ++cursor;
    while(cursor<s.size()&&s[cursor]>='0'&&s[cursor]<='9'){++cursor;++digits;}
  }
  if(digits==0) throw std::invalid_argument("invalid decimal number: "+s);
  if(cursor<s.size()&&(s[cursor]=='e'||s[cursor]=='E')) {
    ++cursor;
    if(cursor<s.size()&&(s[cursor]=='+'||s[cursor]=='-')) ++cursor;
    std::size_t exponent_digits=0;
    while(cursor<s.size()&&s[cursor]>='0'&&s[cursor]<='9'){++cursor;++exponent_digits;}
    if(exponent_digits==0) throw std::invalid_argument("invalid decimal exponent: "+s);
  }
  if(cursor!=s.size()) throw std::invalid_argument("invalid decimal number: "+s);
  std::size_t n=0;
  double v;
  try { v=std::stod(s,&n); } catch(...) { throw std::invalid_argument("invalid number: "+s); }
  if(n!=s.size()||!std::isfinite(v)) throw std::invalid_argument("invalid finite number: "+s);
  return v;
}
bool on_off(const std::string& s) {
  if(s=="on") return true;
  if(s=="off") return false;
  throw std::invalid_argument("expected on or off, got: "+s);
}
struct Rng {
  std::uint32_t value;
  std::uint32_t next() { value=value*1664525u+1013904223u; return value; }
  double unit() { return double(next()>>8)/16777216.0; }
};
Geometry box(std::string id,std::string kind,Vec3 p,Vec3 s,std::string color,bool solid=true) {
  Geometry g{std::move(id),std::move(kind),std::move(color),p,s,solid,{}, {}};
  // Coordinates are local to position; eight corners and twelve triangles.
  for(int z:{-1,1}) for(int y:{-1,1}) for(int x:{-1,1}) {
    g.vertices.insert(g.vertices.end(),{x*s.x/2,y*s.y/2,z*s.z/2});
  }
  g.indices={0,2,3,0,3,1,4,5,7,4,7,6,0,1,5,0,5,4,
             2,6,7,2,7,3,0,4,6,0,6,2,1,3,7,1,7,5};
  return g;
}
void vec(std::ostream& o,Vec3 v) { o<<'['<<v.x<<','<<v.y<<','<<v.z<<']'; }
void str(std::ostream& o,const std::string& v) { o<<'"'<<escape_json(v)<<'"'; }
void strings(std::ostream& o,const std::vector<std::string>& v) {
  o<<'['; for(std::size_t i=0;i<v.size();++i){ if(i)o<<',';str(o,v[i]); }o<<']';
}
} // namespace

Spec parse_prompt(const std::string& prompt) {
  if(prompt.empty()||prompt.size()>512) throw std::invalid_argument("prompt length must be 1..512 bytes");
  Spec spec; spec.prompt=prompt;
  std::istringstream clauses(prompt);
  std::string clause;
  std::unordered_set<std::string> seen;
  int count=0;
  while(std::getline(clauses,clause,';')) {
    if(++count>12) throw std::invalid_argument("at most 12 prompt clauses are allowed");
    clause=trim(clause);
    if(clause.empty()) throw std::invalid_argument("empty prompt clause");
    std::istringstream input(clause);
    std::vector<std::string> tokens;
    std::string token;
    while(input>>token) tokens.push_back(token);
    std::string key=tokens[0];
    if(clause=="floating island"||clause=="island") key="island";
    if(!seen.insert(key).second) throw std::invalid_argument("duplicate clause: "+key);
    if(key=="island" && (clause=="floating island"||clause=="island")) continue;
    if(tokens.size()!=2) throw std::invalid_argument("unsupported clause: "+clause);
    const auto& value=tokens[1];
    if(key=="platforms"||key=="bodies") {
      if(value.find_first_not_of("0123456789")!=std::string::npos||value.size()>3)
        throw std::invalid_argument(key+" requires an integer 0..999");
      const int n=std::stoi(value);
      if(key=="platforms") spec.platforms=n; else spec.bodies=n;
    } else if(key=="bridges") spec.bridges=on_off(value);
    else if(key=="water") spec.water=(value=="puzzle")?true:on_off(value);
    else if(key=="gravity") spec.gravity=number(value);
    else if(key=="wind") spec.wind=number(value);
    else throw std::invalid_argument("unsupported clause: "+clause);
  }
  if(prompt.back()==';') throw std::invalid_argument("empty trailing clause");
  return spec;
}

Action parse_action(const std::string& text) {
  const auto colon=text.find(':'),equal=text.find('=');
  if(colon==std::string::npos||equal==std::string::npos||colon>=equal)
    throw std::invalid_argument("action must be STEP:VALVE=on|off");
  const auto step=text.substr(0,colon);
  if(step.empty()||step.size()>6||step.find_first_not_of("0123456789")!=std::string::npos)
    throw std::invalid_argument("action step must be an integer 0..100000");
  const auto n=std::stoul(step);
  if(n>100000) throw std::invalid_argument("action step exceeds 100000");
  const auto valve=text.substr(colon+1,equal-colon-1);
  if(valve!="feed"&&valve!="gate") throw std::invalid_argument("unknown valve: "+valve);
  return {static_cast<unsigned>(n),valve,on_off(text.substr(equal+1))};
}

World::World(Spec spec,std::uint32_t seed):spec_(std::move(spec)),seed_(seed) {
  auto repair=[&](auto& value,auto lo,auto hi,const std::string& name) {
    auto old=value; value=std::clamp(value,lo,hi);
    if(value!=old) construction_.repairs.push_back(name+" clamped to supported range");
  };
  if(!std::isfinite(spec_.gravity)||!std::isfinite(spec_.wind)) throw std::invalid_argument("spec numbers must be finite");
  repair(spec_.platforms,1,8,"platforms");
  repair(spec_.bodies,1,8,"bodies");
  repair(spec_.gravity,0.5,20.0,"gravity");
  repair(spec_.wind,-5.0,5.0,"wind");
  generate();
  initial_volume_=total_volume();
  construction_=validate();
  if(!construction_.ok) throw std::runtime_error("generated scene failed constraints");
}

void World::generate() {
  Rng rng{seed_};
  geometry_.push_back(box("catch_floor","floor",{0,-10,0},{80,1,80},"#102335"));
  geometry_.push_back(box("island","island",{0,0,0},{18,2,18},"#3d6570"));
  Geometry rock{"island_rock","rock","#274451",{0,-1,0},{18,6,18},false,{}, {}};
  constexpr int n=8;
  for(int i=0;i<n;++i) {
    const double a=i*6.283185307179586/n;
    rock.vertices.insert(rock.vertices.end(),{std::cos(a)*9,-0.05,std::sin(a)*9});
  }
  rock.vertices.insert(rock.vertices.end(),{0,-6,0});
  for(unsigned i=0;i<n;++i) rock.indices.insert(rock.indices.end(),{i,(i+1)%n,n});
  geometry_.push_back(std::move(rock));
  const std::array<Vec3,8> slots={Vec3{12,0.75,0},Vec3{0,0.75,12},Vec3{-12,0.75,0},Vec3{0,0.75,-12},
                                Vec3{12,0.75,12},Vec3{-12,0.75,12},Vec3{-12,0.75,-12},Vec3{12,0.75,-12}};
  for(int i=0;i<spec_.platforms;++i) {
    const auto p=slots[i];
    geometry_.push_back(box("platform_"+std::to_string(i),"platform",p,{5,1.5,5},"#547a87"));
    if(spec_.bridges) {
      if(p.x!=0) geometry_.push_back(box("bridge_x_"+std::to_string(i),"bridge",
          {p.x/2,1.1,p.z},{std::abs(p.x),0.8,1.6},"#a98560"));
      if(p.z!=0) geometry_.push_back(box("bridge_z_"+std::to_string(i),"bridge",
          {0,1.1,p.z/2},{1.6,0.8,std::abs(p.z)},"#a98560"));
    }
    // Six triangular grass blades form deterministic procedural detail.
    for(int j=0;j<6;++j) {
      const double x=(rng.unit()-0.5)*4,z=(rng.unit()-0.5)*4,h=0.25+rng.unit()*0.5;
      Geometry grass{"grass_"+std::to_string(i)+"_"+std::to_string(j),"grass","#8fbc8b",
                     {p.x+x,1.5,p.z+z},{0.14,h,0.14},false,
                     {-0.07,0,0,0.07,0,0,0,h,0,0,0,-0.07,0,0,0.07,0,h,0},{0,1,2,3,4,5}};
      geometry_.push_back(std::move(grass));
    }
  }
  if(spec_.water) {
    reservoirs_={{"source",{-4,0,-4},{3,2,3},2.7,13,18},
                 {"basin",{1,0,-4},{3,2,2},1.9,1,12},
                 {"goal",{6,0,-4},{2,2,2},1.2,0,8}};
    valves_={{"feed",0,1,true,1.8},{"gate",1,2,false,1.2}};
    for(const auto& r:reservoirs_) {
      const double pedestal=r.base_y-1.2;
      if(pedestal>eps) geometry_.push_back(box(r.id+"_pedestal","pedestal",
          {r.position.x,1+pedestal/2,r.position.z},{r.size.x+0.3,pedestal,r.size.z+0.3},"#344b5e"));
      geometry_.push_back(box(r.id+"_floor","reservoir",{r.position.x,r.base_y-0.1,r.position.z},
          {r.size.x+0.3,0.2,r.size.z+0.3},"#526e85"));
      for(int side:{-1,1}) {
        geometry_.push_back(box(r.id+"_wall_x_"+std::to_string(side),"reservoir",
            {r.position.x+side*(r.size.x/2+0.1),r.base_y+r.size.y/2,r.position.z},
            {0.2,r.size.y,r.size.z+0.4},"#526e85"));
        geometry_.push_back(box(r.id+"_wall_z_"+std::to_string(side),"reservoir",
            {r.position.x,r.base_y+r.size.y/2,r.position.z+side*(r.size.z/2+0.1)},
            {r.size.x, r.size.y,0.2},"#526e85"));
      }
    }
    geometry_.push_back(box("feed_pipe","pipe",{-1.5,2.5,-4},{2,0.15,0.15},"#70c9d4",false));
    geometry_.push_back(box("gate_pipe","pipe",{3.75,1.7,-4},{2.5,0.15,0.15},"#70c9d4",false));
  }
  for(int i=0;i<spec_.bodies;++i) {
    Vec3 p;
    if(i==0) p={2,6,2};
    else {
      const auto& slot=slots[(i-1)%spec_.platforms];
      p={slot.x+(rng.unit()-0.5),6+i*0.4,slot.z+(rng.unit()-0.5)};
    }
    Body body{"body_"+std::to_string(i),p,{(rng.unit()-0.5)*0.8,0,(rng.unit()-0.5)*0.8},{0.8,0.8,0.8},false};
    for(int repair=0;repair<16;++repair) {
      bool blocked=false;
      for(const auto& g:geometry_) if(g.solid&&overlap(body.position,body.size,g.position,g.size)) blocked=true;
      for(const auto& b:bodies_) if(overlap(body.position,body.size,b.position,b.size)) blocked=true;
      if(!blocked) break;
      body.position.y+=1;
      construction_.repairs.push_back("raised overlapping spawn "+body.id);
    }
    bodies_.push_back(body);
  }
}

void World::set_valve(const std::string& id,bool open) {
  for(auto& valve:valves_) if(valve.id==id) { valve.open=open; return; }
  throw std::invalid_argument("valve absent in scene: "+id);
}

void World::flow_water() {
  // Pairwise conservative exchange. Each transfer is capacity/source bounded.
  for(const auto& valve:valves_) {
    if(!valve.open) continue;
    auto& a=reservoirs_[valve.from]; auto& b=reservoirs_[valve.to];
    const double head=(a.base_y+a.level())-(b.base_y+b.level());
    if(std::abs(head)<eps) continue;
    auto& source=head>0?a:b; auto& dest=head>0?b:a;
    const double equilibrium=std::abs(head)/(1/source.area()+1/dest.area());
    const double amount=std::min({valve.conductance*std::sqrt(std::abs(head))*dt,
                                 source.volume,dest.capacity-dest.volume,equilibrium});
    if(amount>0) { source.volume-=amount; dest.volume+=amount; }
  }
}

void World::step() {
  contacts_=0;
  for(auto& body:bodies_) {
    body.resting=false;
    body.velocity.x+=spec_.wind*dt;
    body.velocity.y-=spec_.gravity*dt;
    // Axis-separated fixed-step integration against static AABBs.
    for(int a:{0,2,1}) {
      const double delta=axis(body.velocity,a)*dt;
      axis(body.position,a)+=delta;
      if(std::abs(delta)<eps) continue;
      for(const auto& g:geometry_) if(g.solid&&overlap(body.position,body.size,g.position,g.size)) {
        axis(body.position,a)=axis(g.position,a)+(delta>0?-1:1)*(axis(g.size,a)+axis(body.size,a))/2;
        axis(body.velocity,a)=0;
        if(a==1&&delta<0) {
          body.resting=true;
          body.velocity.x*=0.96; body.velocity.z*=0.96;
        }
        ++contacts_;
      }
    }
  }
  // Small bounded sequential impulse/position projection for dynamic contacts.
  for(int iteration=0;iteration<16;++iteration) {
    for(std::size_t i=0;i<bodies_.size();++i) for(std::size_t j=i+1;j<bodies_.size();++j) {
      auto& a=bodies_[i]; auto& b=bodies_[j];
      if(!overlap(a.position,a.size,b.position,b.size)) continue;
      int best=0; double depth=std::numeric_limits<double>::infinity();
      for(int k=0;k<3;++k) {
        const double d=(axis(a.size,k)+axis(b.size,k))/2-std::abs(axis(a.position,k)-axis(b.position,k));
        if(d<depth) { depth=d; best=k; }
      }
      const double sign=axis(a.position,best)<axis(b.position,best)?-1:1;
      if(best==1) {
        auto& lower=sign<0?a:b; auto& upper=sign<0?b:a;
        if(lower.resting) {
          upper.position.y+=depth+eps;
          upper.velocity.y=0;
          upper.resting=true;
          ++contacts_;
          continue;
        }
      }
      axis(a.position,best)+=sign*(depth+eps)/2;
      axis(b.position,best)-=sign*(depth+eps)/2;
      const double velocity=(axis(a.velocity,best)+axis(b.velocity,best))/2;
      axis(a.velocity,best)=velocity; axis(b.velocity,best)=velocity;
      ++contacts_;
    }
    // Projection can push a stacked lower body into the floor: resolve again.
    for(auto& body:bodies_) for(const auto& g:geometry_) {
      if(!g.solid||!overlap(body.position,body.size,g.position,g.size)) continue;
      int best=0; double depth=std::numeric_limits<double>::infinity();
      for(int k=0;k<3;++k) {
        double d=(axis(body.size,k)+axis(g.size,k))/2-std::abs(axis(body.position,k)-axis(g.position,k));
        if(d<depth) { depth=d; best=k; }
      }
      const double sign=axis(body.position,best)<axis(g.position,best)?-1:1;
      axis(body.position,best)+=sign*(depth+eps);
      axis(body.velocity,best)=0;
      if(best==1&&sign>0) body.resting=true;
      ++contacts_;
    }
  }
  flow_water();
  ++step_;
}

double World::total_volume() const {
  double total=0; for(const auto& r:reservoirs_)total+=r.volume;return total;
}
double World::max_penetration() const {
  double result=0;
  for(const auto& b:bodies_) for(const auto& g:geometry_) if(g.solid)
    result=std::max(result,penetration(b.position,b.size,g.position,g.size));
  for(std::size_t i=0;i<bodies_.size();++i)for(std::size_t j=i+1;j<bodies_.size();++j)
    result=std::max(result,penetration(bodies_[i].position,bodies_[i].size,bodies_[j].position,bodies_[j].size));
  return result;
}
Frame World::frame() const {
  return {step_,step_*dt,total_volume(),max_penetration(),contacts_,
    reservoirs_.size()==3&&reservoirs_[2].level()>=1.0,bodies_,reservoirs_,valves_};
}

Validation World::validate() const {
  Validation v=construction_; v.violations.clear();v.checks=0;
  auto check=[&](bool ok,const std::string& reason){++v.checks;if(!ok)v.violations.push_back(reason);};
  check(v.repairs.size()<=static_cast<unsigned>(v.repair_budget),"deterministic repair budget exceeded");
  std::unordered_set<std::string> ids;
  for(const auto& g:geometry_) {
    check(ids.insert(g.id).second,"duplicate geometry id");
    check(finite(g.position)&&finite(g.size)&&g.size.x>0&&g.size.y>0&&g.size.z>0,"invalid geometry bounds: "+g.id);
    check(g.vertices.size()%3==0&&g.indices.size()%3==0,"invalid triangle array: "+g.id);
    bool good=true;for(unsigned index:g.indices)if(index>=g.vertices.size()/3)good=false;
    for(double coordinate:g.vertices)if(!std::isfinite(coordinate))good=false;
    check(good,"invalid mesh coordinate/index: "+g.id);
    if(g.kind=="bridge") check(std::max(g.size.x,g.size.z)<=12,"bridge span exceeds supported bound");
  }
  for(const auto& b:bodies_) check(finite(b.position)&&finite(b.velocity),"nonfinite body state: "+b.id);
  check(max_penetration()<1e-4,"unresolved AABB penetration");
  for(const auto& r:reservoirs_) {
    check(r.area()>0&&r.capacity>0&&std::abs(r.capacity-r.area()*r.size.y)<eps,"invalid reservoir capacity: "+r.id);
    check(std::isfinite(r.volume)&&r.volume>=-eps&&r.volume<=r.capacity+eps,"water outside volume bounds: "+r.id);
  }
  check(std::abs(total_volume()-initial_volume_)<1e-8,"water conservation error");
  v.ok=v.violations.empty(); return v;
}

std::string escape_json(const std::string& s) {
  std::ostringstream o;
  for(unsigned char c:s) {
    switch(c) {case '"':o<<"\\\"";break;case '\\':o<<"\\\\";break;
      case '\n':o<<"\\n";break;case '\r':o<<"\\r";break;case '\t':o<<"\\t";break;
      default:if(c<32)o<<"\\u"<<std::hex<<std::setw(4)<<std::setfill('0')<<int(c)<<std::dec;else o<<char(c);}
  }
  return o.str();
}

std::string World::json(const std::vector<Frame>& frames,unsigned steps) const {
  std::ostringstream o; o.imbue(std::locale::classic());o<<std::setprecision(12);
  const auto validation=validate();
  o<<"{\"project\":\"aether\",\"seed\":"<<seed_<<",\"mode\":\"native\",\"spec\":{\"prompt\":";str(o,spec_.prompt);
  o<<",\"platforms\":"<<spec_.platforms<<",\"bridges\":"<<(spec_.bridges?"true":"false")
   <<",\"water\":"<<(spec_.water?"true":"false")<<",\"bodies\":"<<spec_.bodies
   <<",\"gravity\":"<<spec_.gravity<<",\"wind\":"<<spec_.wind<<"},\"summary\":{\"steps\":"<<steps
   <<",\"dt\":"<<dt<<",\"geometry_count\":"<<geometry_.size()<<",\"body_count\":"<<bodies_.size()
   <<",\"initial_water_total\":"<<initial_volume_<<",\"water_total\":"<<total_volume()
   <<",\"volume_error\":"<<std::abs(total_volume()-initial_volume_)<<",\"max_penetration\":"<<max_penetration()
   <<",\"validation\":{\"ok\":"<<(validation.ok?"true":"false")<<",\"checks\":"<<validation.checks
   <<",\"repair_budget\":"<<validation.repair_budget<<",\"violations\":"; strings(o,validation.violations);
  o<<",\"repairs\":";strings(o,validation.repairs);o<<"}},\"geometry\":[";
  for(std::size_t i=0;i<geometry_.size();++i) {
    const auto& g=geometry_[i]; if(i)o<<',';o<<"{\"id\":";str(o,g.id);o<<",\"kind\":";str(o,g.kind);
    o<<",\"color\":";str(o,g.color);o<<",\"position\":";vec(o,g.position);o<<",\"size\":";vec(o,g.size);
    o<<",\"solid\":"<<(g.solid?"true":"false")<<",\"vertices\":[";
    for(std::size_t j=0;j<g.vertices.size();++j){if(j)o<<',';o<<g.vertices[j];}o<<"],\"indices\":[";
    for(std::size_t j=0;j<g.indices.size();++j){if(j)o<<',';o<<g.indices[j];}o<<"]}";
  }
  o<<"],\"frames\":[";
  for(std::size_t i=0;i<frames.size();++i) {
    const auto& f=frames[i];if(i)o<<',';
    o<<"{\"step\":"<<f.step<<",\"time\":"<<f.time<<",\"contacts\":"<<f.contacts
     <<",\"max_penetration\":"<<f.max_penetration<<",\"puzzle_solved\":"<<(f.puzzle_solved?"true":"false")<<",\"bodies\":[";
    for(std::size_t j=0;j<f.bodies.size();++j){const auto& b=f.bodies[j];if(j)o<<',';o<<"{\"id\":";str(o,b.id);
      o<<",\"position\":";vec(o,b.position);o<<",\"velocity\":";vec(o,b.velocity);o<<",\"size\":";vec(o,b.size);
      o<<",\"resting\":"<<(b.resting?"true":"false")<<'}';}
    o<<"],\"water\":{\"total_volume\":"<<f.total_volume<<",\"reservoirs\":[";
    for(std::size_t j=0;j<f.reservoirs.size();++j){const auto& r=f.reservoirs[j];if(j)o<<',';o<<"{\"id\":";str(o,r.id);
      o<<",\"volume\":"<<r.volume<<",\"capacity\":"<<r.capacity<<",\"base_y\":"<<r.base_y<<",\"level\":"<<r.level()
       <<",\"position\":";vec(o,r.position);o<<",\"size\":";vec(o,r.size);o<<'}';}
    o<<"]},\"valves\":[";
    for(std::size_t j=0;j<f.valves.size();++j){const auto& v=f.valves[j];if(j)o<<',';o<<"{\"id\":";str(o,v.id);o<<",\"open\":"<<(v.open?"true":"false")<<'}';}
    o<<"]}";
  }
  o<<"]}\n";return o.str();
}
} // namespace aether
