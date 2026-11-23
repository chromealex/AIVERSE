// GPU mirror of the native TextureGen default pattern recipes.
// Color palettes are read from TextureGen, and all results pass CPU parity.
#pragma once
namespace aiverse::render::texture_graph::gpu {
inline constexpr const char* patternShader=R"GPU(
float patternFract(float x){return x-floor(x);}
float patternSmooth(float a,float b,float x){float t=clamp((x-a)/(b-a),0.0f,1.0f);return t*t*(3.0f-2.0f*t);}
float2 patternSample(float2 uv,int kind,int n,uint seed,int size){
 float u=uv.x,v=uv.y;
 float x=u*float(n),y=v*float(n);
 int ix=int(floor(x)),iy=int(floor(y));
 float fx=patternFract(x),fy=patternFract(y);
 float t=0.0f,shade=1.0f;
 if(kind==0){} // solid
 else if(kind==1){t=patternSmooth(0.25f,0.75f,fractal(uv,n,4,seed));}
 else if(kind==2){float edge=float(n)/float(size);t=1.0f-patternSmooth(0.5f-edge,0.5f+edge,fx)*(1.0f-patternSmooth(1.0f-edge,1.0f,fx));}
 else if(kind==3){t=float((ix+iy)&1);shade=1.0f+(rand01(ix,iy,seed)-0.5f)*0.15f;}
 else if(kind==4||kind==5){
   int cols=kind==4?max(1,n/2):n;float bx=u*float(cols)+(kind==4&&(iy&1)!=0?0.5f:0.0f);int col=int(floor(bx));float f=patternFract(bx);
   float d=min(min(f,1.0f-f)*float(n)/float(cols),min(fy,1.0f-fy));
   t=1.0f-patternSmooth(0.045f,0.075f,d);
   float grain=(fractal(uv,n*4,3,seed+7u)-0.5f)*0.12f;
   shade=1.0f+(rand01(wrapi(col,cols),iy,seed)-0.5f)*0.3f+grain;
 }
 else if(kind==6){
   bool wide=rand01(ix/2,iy,seed+3u)>0.5f,tall=!wide&&rand01(ix,iy/2,seed+5u)>0.6f;
   if(wide)fx=(fx+float(ix&1))*0.5f;if(tall)fy=(fy+float(iy&1))*0.5f;
   float sx=wide?0.5f:1.0f,sy=tall?0.5f:1.0f;
   float d=min(min(fx,1.0f-fx)/sx,min(fy,1.0f-fy)/sy);
   t=1.0f-patternSmooth(0.015f,0.03f,d);float rivet=0.0f;
   for(int a=0;a<2;++a)for(int b=0;b<2;++b){
     float cx=a==0?0.08f*sx:1.0f-0.08f*sx,cy=b==0?0.08f*sy:1.0f-0.08f*sy;
     float r=length(float2((fx-cx)/sx,(fy-cy)/sy));rivet=max(rivet,1.0f-patternSmooth(0.02f,0.035f,r));
   }
   float scratches=(fractal(uv,n*8,2,seed+11u)-0.5f)*0.08f;
   shade=1.0f+(rand01(wide?ix/2:ix,tall?iy/2:iy,seed)-0.5f)*0.3f+rivet*0.25f+scratches;
 }
 else if(kind==7){float streak=fractal(float2(u*0.0625f,v),n*16,3,seed)*0.7f+fractal(uv,n*2,2,seed+9u)*0.3f;shade=1.0f+(streak-0.5f)*0.3f;}
 else if(kind==8){
   int planks=max(1,n/2);float py=v*float(planks);int plank=int(floor(py));float f=patternFract(py);
   float warp=fractal(uv,4,3,seed+uint(plank))*3.0f;
   float rings=patternFract((f+warp)*6.0f+rand01(plank,0,seed)*4.0f);
   float grain=fractal(float2(u*0.125f,v),64,2,seed+21u);
   t=clamp(patternSmooth(0.6f,1.0f,rings)*0.7f+(grain-0.5f)*0.6f,0.0f,1.0f);
   float gap=patternSmooth(0.0f,0.04f,min(f,1.0f-f));shade=(0.75f+0.25f*gap)*(1.0f+(rand01(plank,1,seed)-0.5f)*0.3f);
 }
 else if(kind==9){t=float(clamp(int((fractal(uv,max(1,n/2),3,seed)-0.3f)/(0.7f-0.3f)*3.0f),0,2));}
 else if(kind==10){t=v;}
 else if(kind==11){float r=length(float2(fx-0.5f,fy-0.5f));t=1.0f-patternSmooth(0.22f,0.25f,r);}
 else if(kind==12){
   n=max(2,n);int rows=max(2,int(floor(float(n)*1.1547f/2.0f+0.5f))*2);
   x=u*float(n);y=v*float(rows);float ys=0.866f*float(rows)/(float(n)*1.1547f);
   float d1=1e9f,d2=1e9f;int bi=0,bj=0;
   for(int dj=-1;dj<=1;++dj)for(int di=-1;di<=1;++di){int j=int(floor(y))+dj,i=int(floor(x))+di;
     float cx=float(i)+((j&1)!=0?1.0f:0.5f),cy=float(j)+0.5f;float d=length(float2(x-cx,(y-cy)*ys));
     if(d<d1){d2=d1;d1=d;bi=i;bj=j;}else if(d<d2)d2=d;
   }
   t=1.0f-patternSmooth(0.03f,0.06f,(d2-d1)*0.5f);shade=1.0f+(rand01(wrapi(bi,n),wrapi(bj,rows),seed)-0.5f)*0.15f;
 }
 else if(kind==13){
   int rows=n*2;y=v*float(rows);int bi=0,bj=-1000000;float bd=1.0f;
   for(int dj=-2;dj<=1;++dj)for(int di=-1;di<=1;++di){int j=int(floor(y))+dj,i=int(floor(x))+di;
     float cx=float(i)+((j&1)!=0?1.0f:0.5f);float d=length(float2(x-cx,(y-float(j))*0.5f));
     if(d<0.62f&&(j>bj||(j==bj&&d<bd))){bj=j;bi=i;bd=d;}
   }
   if(bj==-1000000)t=1.0f;else{t=patternSmooth(0.62f*0.82f,0.62f,bd);shade=1.0f+(rand01(wrapi(bi,n),wrapi(bj,rows),seed)-0.5f)*0.3f;}
 }
 return float2(t,shade);
}
float3 patternValue(float2 uv,int kind,int n,uint seed,int size,bool mask,float3 a,float3 b,float3 c){
 float2 s=patternSample(uv,kind,n,seed,size);float3 v;
 if(mask)v=float3(kind==9?(s.x==1.0f?1.0f:0.0f):s.x);
 else v=(kind==9?(s.x==0.0f?a:(s.x==1.0f?b:c)):a+(b-a)*clamp(s.x,0.0f,1.0f))*max(0.0f,s.y);
 // TextureGen writes RGBA8 before the graph consumes Pattern/PatternMask.
 return floor(clamp(v,0.0f,1.0f)*255.0f+0.5f)/255.0f;
}
)GPU";
}
