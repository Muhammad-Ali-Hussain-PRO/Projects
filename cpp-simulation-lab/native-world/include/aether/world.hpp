#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace aether {
struct Vec3 { double x=0, y=0, z=0; };
struct Spec {
  int platforms=4, bodies=3;
  bool bridges=true, water=true;
  double gravity=9.81, wind=0;
  std::string prompt="floating island; platforms 4; bridges on; water puzzle; bodies 3";
};
struct Validation {
  bool ok=true;
  int checks=0, repair_budget=32;
  std::vector<std::string> violations, repairs;
};
struct Geometry {
  std::string id, kind, color;
  Vec3 position, size;
  bool solid=true;
  std::vector<double> vertices;
  std::vector<unsigned> indices;
};
struct Body { std::string id; Vec3 position, velocity, size; bool resting=false; };
struct Reservoir {
  std::string id;
  Vec3 position, size;
  double base_y=0, volume=0, capacity=0;
  double area() const { return size.x*size.z; }
  double level() const { return volume/area(); }
};
struct Valve { std::string id; unsigned from=0, to=0; bool open=false; double conductance=1; };
struct Action { unsigned step=0; std::string valve; bool open=false; };
struct Frame {
  unsigned step=0;
  double time=0, total_volume=0, max_penetration=0;
  int contacts=0;
  bool puzzle_solved=false;
  std::vector<Body> bodies;
  std::vector<Reservoir> reservoirs;
  std::vector<Valve> valves;
};
class World {
public:
  static constexpr double dt=1.0/120.0;
  World(Spec spec, std::uint32_t seed);
  void step();
  void set_valve(const std::string& id, bool open);
  Frame frame() const;
  Validation validate() const;
  std::string json(const std::vector<Frame>& frames, unsigned steps) const;
  const Spec& spec() const { return spec_; }
  const std::vector<Geometry>& geometry() const { return geometry_; }
  const std::vector<Body>& bodies() const { return bodies_; }
  const std::vector<Reservoir>& reservoirs() const { return reservoirs_; }
  double total_volume() const;
  double initial_volume() const { return initial_volume_; }
  std::uint32_t seed() const { return seed_; }
private:
  Spec spec_;
  std::uint32_t seed_;
  unsigned step_=0;
  int contacts_=0;
  double initial_volume_=0;
  Validation construction_;
  std::vector<Geometry> geometry_;
  std::vector<Body> bodies_;
  std::vector<Reservoir> reservoirs_;
  std::vector<Valve> valves_;
  void generate();
  void flow_water();
  double max_penetration() const;
};
Spec parse_prompt(const std::string& prompt);
Action parse_action(const std::string& text);
std::string escape_json(const std::string& text);
} // namespace aether
