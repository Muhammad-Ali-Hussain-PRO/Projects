#pragma once
#include <cctype>
#include <cstdlib>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>

namespace processlens {
// Exact arithmetic within a deliberately bounded domain. Operands are capped
// at 1e9 after reduction, so cross-products and their sums fit signed int64.
struct Rational {
    long long n,d;
    Rational(long long numerator=0,long long denominator=1):n(numerator),d(denominator) {
        if(d==0 || n < -2000000000000000000LL || n>2000000000000000000LL ||
           d < -2000000000000000000LL || d>2000000000000000000LL)
            throw std::invalid_argument("zero denominator or arithmetic bound exceeded");
        if(d<0) { n=-n; d=-d; }
        auto g=std::gcd(n,d); n/=g; d/=g;
        if(std::abs(n)>1000000000LL || d>1000000000LL)
            throw std::invalid_argument("reduced rational exceeds 1e9 bound");
    }
    std::string str() const {return std::to_string(n)+(d==1?"":"/"+std::to_string(d));}
    bool operator==(const Rational& b)const{return n==b.n && d==b.d;}
};
inline Rational add(Rational a,Rational b){return {a.n*b.d+b.n*a.d,a.d*b.d};}
inline Rational subtract(Rational a,Rational b){return {a.n*b.d-b.n*a.d,a.d*b.d};}
inline Rational multiply(Rational a,Rational b){return {a.n*b.n,a.d*b.d};}
inline Rational divide(Rational a,Rational b){return {a.n*b.d,a.d*b.n};}
using Environment=std::map<std::string,Rational>;

class Parser {
    const std::string& text; const Environment& env; size_t pos=0;
    void space(){while(pos<text.size() && std::isspace(static_cast<unsigned char>(text[pos])))++pos;}
    bool take(char c){space();if(pos<text.size() && text[pos]==c){++pos;return true;}return false;}
    Rational atom(){
        space();
        if(take('+'))return atom();
        if(take('-')){auto x=atom();return {-x.n,x.d};}
        if(take('(')){auto x=expression();if(!take(')'))throw std::invalid_argument("missing closing parenthesis");return x;}
        if(pos>=text.size())throw std::invalid_argument("missing operand");
        if(std::isdigit(static_cast<unsigned char>(text[pos]))){
            long long n=0;
            while(pos<text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))){
                n=n*10+(text[pos++]-'0');
                if(n>1000000000LL)throw std::invalid_argument("integer literal exceeds 1e9");
            }
            return {n};
        }
        if(std::isalpha(static_cast<unsigned char>(text[pos])) || text[pos]=='_'){
            size_t start=pos++;
            while(pos<text.size() && (std::isalnum(static_cast<unsigned char>(text[pos])) || text[pos]=='_'))++pos;
            auto name=text.substr(start,pos-start);
            auto it=env.find(name);
            if(it==env.end())throw std::invalid_argument("undefined symbol: "+name);
            return it->second;
        }
        throw std::invalid_argument("unsupported arithmetic syntax");
    }
    Rational term(){auto x=atom();for(;;){if(take('*'))x=multiply(x,atom());else if(take('/'))x=divide(x,atom());else return x;}}
    Rational expression(){auto x=term();for(;;){if(take('+'))x=add(x,term());else if(take('-'))x=subtract(x,term());else return x;}}
public:
    Parser(const std::string& s,const Environment& e):text(s),env(e){}
    Rational run(){if(text.size()>4096)throw std::invalid_argument("expression too long");auto x=expression();space();if(pos!=text.size())throw std::invalid_argument("trailing syntax");return x;}
};
struct Step {std::string name,expression;Rational claimed;};
struct Verdict {bool accepted;std::string feedback,expected;};
class Validator {
    Environment env;
public:
    const Environment& bindings()const{return env;}
    Verdict validate(const Step& s){
        if(s.name.empty() || !(std::isalpha(static_cast<unsigned char>(s.name[0])) || s.name[0]=='_'))
            return {false,"invalid step symbol",""};
        for(char c:s.name)if(!(std::isalnum(static_cast<unsigned char>(c)) || c=='_'))return {false,"invalid step symbol",""};
        if(env.count(s.name))return {false,"symbol already committed",""};
        try {
            auto actual=Parser(s.expression,env).run();
            if(!(actual==s.claimed))return {false,"arithmetic mismatch",actual.str()};
            env.emplace(s.name,actual);
            return {true,"verified exact arithmetic",actual.str()};
        }catch(const std::invalid_argument& e){return {false,e.what(),""};}
    }
};
} // namespace processlens
