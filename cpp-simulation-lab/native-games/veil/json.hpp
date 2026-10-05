#pragma once
#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <cctype>
#include <cmath>
#include <sstream>
namespace minijson {
struct Value {
 enum Kind {Null,Bool,Number,String,Array,Object} kind=Null;
 bool b=false; double n=0; std::string s; std::vector<Value> a; std::map<std::string,Value> o;
 const Value& at(const std::string& k) const { auto p=o.find(k); if(kind!=Object||p==o.end())throw std::runtime_error("missing field: "+k);return p->second; }
 bool has(const std::string& k) const{return kind==Object&&o.count(k);}
 int integer()const{if(kind!=Number||n!=std::floor(n)||n<-2147483648.0||n>2147483647.0)throw std::runtime_error("expected integer");return static_cast<int>(n);}
 std::string string()const{if(kind!=String)throw std::runtime_error("expected string");return s;}
};
class Parser {
 const std::string& s; size_t p=0; int depth=0;
 void ws(){while(p<s.size()&&std::isspace(static_cast<unsigned char>(s[p])))++p;}
 char take(){if(p>=s.size())throw std::runtime_error("unexpected end of JSON");return s[p++];}
 std::string str(){if(take()!='"')throw std::runtime_error("expected string");std::string out;while(p<s.size()){char c=take();if(c=='"')return out;if(static_cast<unsigned char>(c)<32)throw std::runtime_error("control character");if(c=='\\'){char e=take();switch(e){case '"':case '\\':case '/':out+=e;break;case 'b':out+='\b';break;case 'f':out+='\f';break;case 'n':out+='\n';break;case 'r':out+='\r';break;case 't':out+='\t';break;case 'u':{unsigned code=0;for(int i=0;i<4;i++){char h=take();int d=isdigit(static_cast<unsigned char>(h))?h-'0':h>='a'&&h<='f'?h-'a'+10:h>='A'&&h<='F'?h-'A'+10:-1;if(d<0)throw std::runtime_error("invalid unicode escape");code=code*16+d;}if(code>=0xd800&&code<=0xdfff)throw std::runtime_error("surrogate escapes unsupported");if(code<128)out+=char(code);else if(code<2048){out+=char(192|(code>>6));out+=char(128|(code&63));}else{out+=char(224|(code>>12));out+=char(128|((code>>6)&63));out+=char(128|(code&63));}break;}default:throw std::runtime_error("bad escape");}}else out+=c;}throw std::runtime_error("unterminated string");}
 Value val(){ws();if(++depth>32)throw std::runtime_error("JSON nesting limit");Value v;if(p>=s.size())throw std::runtime_error("missing value");char c=s[p];if(c=='{'){v.kind=Value::Object;++p;ws();if(p<s.size()&&s[p]=='}')++p;else for(;;){ws();std::string k=str();ws();if(take()!=':')throw std::runtime_error("expected colon");if(v.o.count(k))throw std::runtime_error("duplicate field");v.o[k]=val();ws();c=take();if(c=='}')break;if(c!=',')throw std::runtime_error("expected comma");}}else if(c=='['){v.kind=Value::Array;++p;ws();if(p<s.size()&&s[p]==']')++p;else for(;;){v.a.push_back(val());ws();c=take();if(c==']')break;if(c!=',')throw std::runtime_error("expected comma");}}else if(c=='"'){v.kind=Value::String;v.s=str();}else if(s.compare(p,4,"true")==0){v.kind=Value::Bool;v.b=true;p+=4;}else if(s.compare(p,5,"false")==0){v.kind=Value::Bool;p+=5;}else if(s.compare(p,4,"null")==0){p+=4;}else{size_t begin=p;if(c=='-')++p;if(p>=s.size()||!isdigit(static_cast<unsigned char>(s[p])))throw std::runtime_error("invalid value");if(s[p]=='0')++p;else while(p<s.size()&&isdigit(static_cast<unsigned char>(s[p])))++p;if(p<s.size()&&s[p]=='.'){++p;size_t q=p;while(p<s.size()&&isdigit(static_cast<unsigned char>(s[p])))++p;if(q==p)throw std::runtime_error("invalid fraction");}if(p<s.size()&&(s[p]=='e'||s[p]=='E')){++p;if(p<s.size()&&(s[p]=='+'||s[p]=='-'))++p;size_t q=p;while(p<s.size()&&isdigit(static_cast<unsigned char>(s[p])))++p;if(q==p)throw std::runtime_error("invalid exponent");}v.kind=Value::Number;v.n=std::stod(s.substr(begin,p-begin));if(!std::isfinite(v.n))throw std::runtime_error("nonfinite number");}--depth;return v;}
 public:explicit Parser(const std::string& x):s(x){} Value parse(){if(s.size()>65536)throw std::runtime_error("JSON input limit");Value v=val();ws();if(p!=s.size())throw std::runtime_error("trailing JSON");return v;}
};
inline Value parse(const std::string& s){return Parser(s).parse();}
inline std::string quote(const std::string& s){std::ostringstream o;o<<'"';for(unsigned char c:s){switch(c){case '"':o<<"\\\"";break;case '\\':o<<"\\\\";break;case '\n':o<<"\\n";break;case '\r':o<<"\\r";break;case '\t':o<<"\\t";break;default:if(c<32){const char* h="0123456789abcdef";o<<"\\u00"<<h[c>>4]<<h[c&15];}else o<<c;}}o<<'"';return o.str();}
}
