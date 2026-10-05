#pragma once
#include <algorithm>
#include <cmath>
#include <list>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
namespace vectorgate {
using Embedding=std::vector<double>;
struct Namespace {
    std::string tenant,auth_scope,model,model_revision,system_digest,tools_digest,
                decoding_digest,knowledge_revision,embedding_revision;
    auto fields()const{return std::tie(tenant,auth_scope,model,model_revision,system_digest,
        tools_digest,decoding_digest,knowledge_revision,embedding_revision);}
    bool operator==(const Namespace& other)const{return fields()==other.fields();}
    void validate()const{
        for(const auto* field:{&tenant,&auth_scope,&model,&model_revision,&system_digest,&tools_digest,
                              &decoding_digest,&knowledge_revision,&embedding_revision})
            if(field->empty())throw std::invalid_argument("namespace metadata must be complete");
    }
};
inline Embedding normalize(Embedding x,size_t dimension){
    if(x.size()!=dimension || dimension==0)throw std::invalid_argument("embedding dimension mismatch");
    double norm=0;
    for(double v:x){if(!std::isfinite(v))throw std::invalid_argument("nonfinite embedding");norm=std::hypot(norm,v);}
    if(norm==0 || !std::isfinite(norm))throw std::invalid_argument("zero or excessive embedding norm");
    for(double& v:x)v/=norm;
    return x;
}
inline double cosine(const Embedding& a,const Embedding& b){
    if(a.empty() || a.size()!=b.size())throw std::invalid_argument("cosine shape mismatch");
    double value=0;for(size_t i=0;i<a.size();++i)value+=a[i]*b[i];
    return std::clamp(value,-1.0,1.0);
}
struct Hit {std::string response;double similarity;bool exact_request;};
class Cache {
    struct Entry {Namespace scope;std::string request,response;Embedding vector;long long expires;};
    size_t capacity,dimension;double threshold;std::list<Entry> entries; // front is most recently used
    void expire(long long now){entries.remove_if([&](const Entry& e){return e.expires<=now;});}
public:
    Cache(size_t max_entries,size_t dims,double minimum_cosine):capacity(max_entries),dimension(dims),threshold(minimum_cosine){
        if(!capacity || !dimension || !std::isfinite(threshold) || threshold<0 || threshold>1)
            throw std::invalid_argument("invalid cache configuration");
    }
    size_t size()const{return entries.size();}
    void put(const Namespace& scope,const std::string& request,Embedding embedding,const std::string& response,
             long long now,long long expires){
        scope.validate();auto unit=normalize(std::move(embedding),dimension);
        if(expires<=now)throw std::invalid_argument("expiry must be in the future");
        expire(now);
        entries.remove_if([&](const Entry& e){return e.scope==scope && e.request==request;});
        entries.push_front({scope,request,response,std::move(unit),expires});
        while(entries.size()>capacity)entries.pop_back();
    }
    std::optional<Hit> get(const Namespace& scope,const std::string& request,Embedding embedding,long long now){
        scope.validate();auto unit=normalize(std::move(embedding),dimension);expire(now);
        auto best=entries.end();double score=threshold;
        for(auto it=entries.begin();it!=entries.end();++it){
            if(!(it->scope==scope))continue; // exact metadata check BEFORE similarity
            double s=cosine(unit,it->vector);
            if(it->request==request){best=it;score=s;break;}
            if(s>=score && (best==entries.end() || s>score)){best=it;score=s;}
        }
        if(best==entries.end())return std::nullopt;
        Hit result{best->response,score,best->request==request};
        entries.splice(entries.begin(),entries,best);
        return result;
    }
};
} // namespace vectorgate
