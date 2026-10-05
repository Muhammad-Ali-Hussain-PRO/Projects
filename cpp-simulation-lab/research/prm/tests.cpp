#include "validator.hpp"
#include "../test_support.hpp"
int main(){
    using namespace processlens;
    Tests t; Validator v;
    t.check(v.validate({"a","3 * (4 + 2)",{18}}).accepted,"parentheses and multiplication verified");
    t.check(v.validate({"b","a / 4",{9,2}}).accepted,"committed dependency with exact fraction");
    t.check(v.validate({"c","-b + 1 / 2",{-4}}).accepted,"unary sign and fractional arithmetic");
    t.check(v.validate({"d","2 + 3 * 4",{14}}).accepted,"multiplication precedes addition");
    auto wrong=v.validate({"wrong","a + 2",{21}});
    t.check(!wrong.accepted && wrong.expected=="20","incorrect intermediate step identifies expected value");
    t.check(v.bindings().count("wrong")==0,"rejected step leaves environment unchanged");
    t.check(!v.validate({"e","wrong - 1",{20}}).accepted,"downstream dependency on rejected step blocked");
    t.check(!v.validate({"zero","1 / (2 - 2)",{0}}).accepted,"division by zero rejected");
    t.check(!v.validate({"a","5",{5}}).accepted && v.bindings().at("a")==Rational(18),"committed symbols cannot be overwritten");
    t.check(!v.validate({"unsupported","2 ^ 3",{8}}).accepted,"unsupported power syntax rejected");
    t.check(!v.validate({"trailing","2 + 3 junk",{5}}).accepted,"trailing text rejected");
    t.check(!v.validate({"overflow","1000000000 * 1000000000",{0}}).accepted,"bounded arithmetic rejects excessive reduced result");
    t.check(Rational(2,4)==Rational(1,2),"rational normalization is exact");
    t.check(v.validate({"recover","6 / 3 + a",{20}}).accepted,"valid independent step can follow rejection");
    t.report("ProcessLens PRM",",\n  \"method\": \"deterministic exact arithmetic validator; no trained model\"");
}
