#pragma once
namespace aiverse::render::screenlighting {
// Shared GLSL/MSL bodies. UVs are in projected bottom-up space; texture reads flip on Metal.
inline const char* common = R"(
#define UV(p) (px.z > 0.5 ? vec2((p).x, 1.0-(p).y) : (p))
// Accumulate HDR in a bounded luminance domain so tiny hot specular samples do not dominate.
vec3 taaCompress(vec3 c) { c=max(c,vec3(0.0)); return c/(1.0+dot(c,vec3(0.2126,0.7152,0.0722))); }
float taaKernel(float x) {
    x=abs(x);
    return x<1.0 ? (1.5*x-2.5)*x*x+1.0 : ((-0.5*x+2.5)*x-4.0)*x+2.0;
}
vec3 taaExpand(vec3 c) { return c/max(1.0-dot(c,vec3(0.2126,0.7152,0.0722)),0.0001); }
#define WORLD(p,d) (invVP * vec4((p)*2.0-1.0, (d)*px.w+(1.0-px.w), 1.0))
)";
inline const char* bounce = R"(
    vec4 center = COLOR(UV(s));
    float depth = DEPTH(UV(s));
    if (depth >= 0.99999) RETURN(center);
    vec4 hp = WORLD(s,depth); vec3 p = hp.xyz/hp.w;
    vec2 dx = vec2(px.x,0.0), dy = vec2(0.0,px.y);
    vec4 hx = WORLD(s+dx,DEPTH(UV(s+dx))), hy = WORLD(s+dy,DEPTH(UV(s+dy)));
    vec3 n = cross(hx.xyz/hx.w-p,hy.xyz/hy.w-p);
    n *= 1.0/sqrt(max(dot(n,n),1e-12));
    if (dot(n,eye.xyz-p)<0.0) n=-n;
    vec3 indirect = vec3(0.0);
    float footprint = min(96.0, 500.0/max(length(p-eye.xyz),1.0));
    for (int i=0;i<16;++i) {
        float angle = float(i)*2.39996323;
        float radius = sqrt((float(i)+0.5)/16.0)*footprint;
        vec2 q = s+vec2(cos(angle)*px.x,sin(angle)*px.y)*radius;
        if (min(q.x,q.y)<0.0 || max(q.x,q.y)>1.0) continue;
        float zd = DEPTH(UV(q)); if(zd>=0.99999) continue;
        vec4 hq = WORLD(q,zd); vec3 delta = hq.xyz/hq.w-p;
        float r2=dot(delta,delta); if(r2<0.0025 || r2>9.0) continue;
        vec4 hqx=WORLD(q+dx,DEPTH(UV(q+dx))), hqy=WORLD(q+dy,DEPTH(UV(q+dy)));
        vec3 qn=cross(hqx.xyz/hqx.w-hq.xyz/hq.w,hqy.xyz/hqy.w-hq.xyz/hq.w);
        qn *= 1.0/sqrt(max(dot(qn,qn),1e-12));
        if(dot(qn,eye.xyz-hq.xyz/hq.w)<0.0) qn=-qn;
        vec3 direction=delta/sqrt(r2);
        float weight=max(dot(n,direction),0.0)*max(dot(qn,-direction),0.0)*(1.0-r2/9.0)/(1.0+r2);
        indirect += min(COLOR(UV(q)).rgb,vec3(8.0))*weight;
    }
    // No albedo G-buffer: bounded receiver tint is an approximation, never recursive feedback.
    vec3 tint=clamp(center.rgb,vec3(0.05),vec3(0.8));
    RETURN(vec4(center.rgb+indirect*tint*(2.0/16.0),center.a));
)";
inline const char* temporal = R"(
    // Write history in stable, unjittered output coordinates.
    s+=jitter.xy;
    vec3 now=taaCompress(COLOR(UV(s)).rgb);
    float zd=DEPTH(UV(s)); vec4 hp=WORLD(s,zd); vec3 p=hp.xyz/hp.w;
    float distance=zd<0.99999 ? min(length(p-eye.xyz),65000.0) : 0.0;
    vec3 lo=now,hi=now;
    float nearHistory=65000.0,farHistory=0.0;bool skyNeighbor=false;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) {
        vec2 q=s+vec2(float(x)*px.x,float(y)*px.y);
        vec3 v=taaCompress(COLOR(UV(q)).rgb);
        lo=min(lo,v); hi=max(hi,v);
        float z=DEPTH(UV(q));
        if(z>=0.99999)skyNeighbor=true;
        else {
            vec4 hq=WORLD(q,z);float expected=length(hq.xyz/hq.w-prevEye.xyz);
            nearHistory=min(nearHistory,expected);farHistory=max(farHistory,expected);
        }
    }
    // At depth edges retain the coverage of both nearby surfaces, including the sky.
    // A center-depth-only test resets accumulation whenever a subpixel object disappears.
    vec3 previousPoint=distance>0.0?p:p-eye.xyz+prevEye.xyz;
    vec4 oldClip=prevVP*vec4(previousPoint,1.0);
    vec2 oldUv=oldClip.xy/max(oldClip.w,0.00001)*0.5+0.5-jitter.zw;
    float weight=0.0;
    vec3 old=now;
    if(params.x>0.5 && oldClip.w>0.0 && min(oldUv.x,oldUv.y)>0.0 && max(oldUv.x,oldUv.y)<1.0) {
        // Depth must not be bilinearly averaged across a foreground/background edge.
        // Validate taps independently. Cubic reconstruction avoids the progressive blur of bilinear history reads.
        vec2 pixel=oldUv/px.xy-vec2(0.5), base=floor(pixel), fraction=fract(pixel);
        vec3 sum=vec3(0.0);float accepted=0.0;
        float tolerance=max(0.03,farHistory*0.003);
        for(int y=-1;y<=2;++y) for(int x=-1;x<=2;++x) {
            vec2 q=(base+vec2(float(x),float(y))+vec2(0.5))*px.xy;
            if(min(q.x,q.y)<=0.0 || max(q.x,q.y)>=1.0)continue;
            vec4 prior=HISTORY(UV(q));
            float w=taaKernel(float(x)-fraction.x)*taaKernel(float(y)-fraction.y);
            if((prior.a==0.0&&skyNeighbor)||(prior.a>0.0&&prior.a>=nearHistory-tolerance&&prior.a<=farHistory+tolerance)) {
                sum+=taaCompress(prior.rgb)*w;accepted+=w;
            }
        }
        if(accepted>0.05) {
            old=clamp(sum/accepted,lo,hi);
            // High contrast is precisely where subpixel coverage needs temporal accumulation.
            // Depth validation and neighborhood clipping reject stale history instead.
            weight=0.9;
        }
    }
    RETURN(vec4(taaExpand(mix(now,old,weight)),distance));
)";
inline const char* copy = R"( RETURN(vec4(COLOR(UV(s)).rgb,1.0)); )";
}
