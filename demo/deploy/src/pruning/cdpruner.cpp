#include "cdpruner.h"
#include <cmath>
#include <vector>
#include <algorithm>
static float dott(const float* a,const float* b,int d){float s=0;for(int i=0;i<d;i++)s+=a[i]*b[i];return s;}
size_t cdpruner_select(float* v,size_t n,int dim,size_t k){
    k=std::max((size_t)1,std::min(k,n));
    std::vector<float> ker(n*n);
    for(size_t i=0;i<n;i++){float inv=1.0f/(std::sqrt(dott(v+i*dim,v+i*dim,dim))+1e-8f);for(int j=0;j<dim;j++)v[i*dim+j]*=inv;}
    for(size_t i=0;i<n;i++)for(size_t j=0;j<n;j++)ker[i*n+j]=dott(v+i*dim,v+j*dim,dim);
    std::vector<float> di2s(n);
    for(size_t i=0;i<n;i++)di2s[i]=ker[i*n+i];
    std::vector<std::vector<float>> cis(k,std::vector<float>(n,0));
    std::vector<size_t> sel(k);
    for(size_t t=0;t<k;t++){
        size_t best=0;float bv=-1e9f;
        for(size_t i=0;i<n;i++){if(di2s[i]>bv){bv=di2s[i];best=i;}}
        sel[t]=best;
        float inv=1.0f/std::sqrt(std::max(di2s[best],1e-12f));
        for(size_t i=0;i<n;i++){float prev=0;for(size_t p=0;p<t;p++)prev+=cis[p][best]*cis[p][i];cis[t][i]=(ker[best*n+i]-prev)*inv;di2s[i]-=cis[t][i]*cis[t][i];}
        di2s[best]=-1e9f;
    }
    std::sort(sel.begin(),sel.end());
    std::vector<float> buf(k*dim);
    for(size_t i=0;i<k;i++)std::copy(v+sel[i]*dim,v+(sel[i]+1)*dim,buf.data()+i*dim);
    std::copy(buf.data(),buf.data()+k*dim,v);
    return k;
}
