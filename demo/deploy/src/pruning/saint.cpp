#include "saint.h"
#include <cmath>
#include <vector>
#include <numeric>
#include <algorithm>
static float csim(const float* a,const float* b,int d){
    float dt=0,na=0,nb=0;
    for(int i=0;i<d;i++){dt+=a[i]*b[i];na+=a[i]*a[i];nb+=b[i]*b[i];}
    return dt/(std::sqrt(na)*std::sqrt(nb)+1e-8f);
}
size_t saint_select(float* v,size_t n,int dim,size_t k){
    k=std::max((size_t)1,std::min(k,n));
    for(size_t i=0;i<n;i++){float inv=1.0f/(std::sqrt(std::inner_product(v+i*dim,v+i*dim+dim,v+i*dim,0.0f))+1e-8f);for(int j=0;j<dim;j++)v[i*dim+j]*=inv;}
    size_t ns=std::min(n,(size_t)50);
    std::vector<float> sim(n,0);
    for(size_t i=0;i<n;i++){for(size_t j=0;j<ns;j++)sim[i]+=std::abs(csim(v+i*dim,v+j*dim,dim));sim[i]/=ns;}
    std::vector<float> sorted=sim;std::sort(sorted.begin(),sorted.end());
    float th=sorted[n/2]*0.8f;
    std::vector<size_t> idx(n);std::iota(idx.begin(),idx.end(),0);
    size_t act=0;
    for(size_t i=0;i<n;i++)if(sim[i]<th)idx[act++]=i;
    if(act<k){std::partial_sort(idx.begin()+act,idx.begin()+k,idx.end(),[&](size_t a,size_t b){return sim[a]<sim[b];});act=k;}
    std::sort(idx.begin(),idx.begin()+act);
    std::vector<float> buf(act*dim);
    for(size_t i=0;i<act;i++)std::copy(v+idx[i]*dim,v+(idx[i]+1)*dim,buf.data()+i*dim);
    std::copy(buf.data(),buf.data()+act*dim,v);
    return act;
}
