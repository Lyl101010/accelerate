#include "dart.h"
#include <cmath>
#include <vector>
#include <numeric>
#include <algorithm>
static float csim(const float* a,const float* b,int d){
    float dt=0,na=0,nb=0;
    for(int i=0;i<d;i++){dt+=a[i]*b[i];na+=a[i]*a[i];nb+=b[i]*b[i];}
    return dt/(std::sqrt(na)*std::sqrt(nb)+1e-8f);
}
size_t dart_select(float* v,size_t n,int dim,size_t k){
    k=std::max((size_t)1,std::min(k,n));
    size_t np=std::min(n,(size_t)20);
    std::vector<float> score(n,0);
    for(size_t i=0;i<n;i++){for(size_t p=0;p<np;p++)score[i]+=std::abs(csim(v+i*dim,v+p*dim,dim));score[i]/=np;}
    std::vector<size_t> idx(n);std::iota(idx.begin(),idx.end(),0);
    std::partial_sort(idx.begin(),idx.begin()+k,idx.end(),[&](size_t a,size_t b){return score[a]<score[b];});
    std::sort(idx.begin(),idx.begin()+k);
    std::vector<float> buf(k*dim);
    for(size_t i=0;i<k;i++)std::copy(v+idx[i]*dim,v+(idx[i]+1)*dim,buf.data()+i*dim);
    std::copy(buf.begin(),buf.end(),v);
    return k;
}
