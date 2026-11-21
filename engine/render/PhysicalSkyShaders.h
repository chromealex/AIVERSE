#pragma once
namespace aiverse::render::physicalsky {
// One common GLSL/MSL implementation: metres, Y up, linear radiance.
inline const char* helpers=R"(
float skyHash(vec3 p){p=fract(p*.1031);p+=dot(p,p.yzx+33.33);return fract((p.x+p.y)*p.z);}
float skyNoise(vec3 p){
    vec3 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
    return mix(mix(mix(skyHash(i),skyHash(i+vec3(1,0,0)),f.x),
                   mix(skyHash(i+vec3(0,1,0)),skyHash(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(skyHash(i+vec3(0,0,1)),skyHash(i+vec3(1,0,1)),f.x),
                   mix(skyHash(i+vec3(0,1,1)),skyHash(i+vec3(1,1,1)),f.x),f.y),f.z);
}
float skyPhase(float mu,float g){return (1.0-g*g)/(12.566371*pow(max(1.0+g*g-2.0*g*mu,.001),1.5));}
vec2 skySphere(vec3 o,vec3 d,float r){float b=dot(o,d),c=dot(o,o)-r*r,h=b*b-c;if(h<0.0)return vec2(1,-1);h=sqrt(h);return vec2(-b-h,-b+h);}
// Stable intersections also for cameras inside/above a cloud layer and horizontal rays.
vec2 skyLayer(vec3 o,vec3 d,vec4 layer,float cap){
    if(abs(d.y)<.00001)return o.y>=layer.x&&o.y<=layer.x+layer.y?vec2(0,cap):vec2(1,-1);
    float a=(layer.x-o.y)/d.y,b=(layer.x+layer.y-o.y)/d.y;
    return vec2(max(0.0,min(a,b)),min(cap,max(a,b)));
}
float skyDensity(vec3 p,vec4 layer,vec4 weather){
    float h=(p.y-layer.x)/layer.y;
    if(h<=0.0||h>=1.0||layer.w<=0.0)return 0.0;
    vec3 q=(p+vec3(weather.x,0,weather.y))/layer.z;
    q.y*=2.0;
    float n=.57*skyNoise(q)+.28*skyNoise(q*2.03)+.15*skyNoise(q*4.11);
    float shape=smoothstep(0.0,.18,h)*(1.0-smoothstep(.65,1.0,h));
    float erosion=.12*skyNoise(q*9.3);
    return clamp((n-(1.0-layer.w)-erosion)/.2,0.0,1.0)*shape;
}
float skyCloudSun(vec3 p,vec3 sun,vec4 layer,vec4 weather,float samples){
    if(sun.y<=0.0)return 0.0;
    vec2 hit=skyLayer(p,sun,layer,50000.0);
    if(hit.y<=hit.x)return 1.0;
    float stepLen=(hit.y-hit.x)/samples,tau=0.0;
    for(int j=0;j<6;++j){if(float(j)>=samples)break;
        tau+=skyDensity(p+sun*(hit.x+(float(j)+.5)*stepLen),layer,weather)*stepLen*weather.z;
    }
    return exp(-tau);
}
vec3 skySunTransmission(float altitude,vec3 sun,vec4 air){
    vec3 p=vec3(0,6360000.0+max(altitude,1.0),0);
    if(skySphere(p,sun,6360000.0).x>0.0)return vec3(0);
    float ds=max(skySphere(p,sun,6460000.0).y,0.0)/8.0;
    vec2 optical=vec2(0);
    for(int j=0;j<8;++j){float h=max(length(p+sun*((float(j)+.5)*ds))-6360000.0,0.0);optical+=exp(-h/vec2(8000,1200))*ds;}
    return exp(-vec3(.000005802,.000013558,.000033100)*air.x*optical.x-vec3(.000003996)*air.y*1.1*optical.y);
}
// Single scattering in an exponential spherical Earth atmosphere (Rayleigh + Mie).
vec3 skyAtmosphere(vec3 eye,vec3 dir,vec3 sun,vec4 air,float steps){
    vec3 o=vec3(0,6360000.0+max(eye.y,1.0),0);
    vec2 hit=skySphere(o,dir,6460000.0);
    if(hit.y<=0.0)return vec3(0);
    vec2 ground=skySphere(o,dir,6360000.0);
    float reach=hit.y;
    if(ground.x>0.0)reach=min(reach,ground.x);
    float ds=reach/steps;vec2 optical=vec2(0);vec3 sumR=vec3(0),sumM=vec3(0);
    vec3 betaR=vec3(.000005802,.000013558,.000033100)*air.x;
    vec3 betaM=vec3(.000003996)*air.y;
    for(int i=0;i<20;++i){if(float(i)>=steps)break;
        vec3 p=o+dir*((float(i)+.5)*ds);float altitude=max(length(p)-6360000.0,0.0);
        vec2 den=exp(-altitude/vec2(8000,1200));
        optical+=den*ds*.5;
        vec2 sunlight=skySphere(p,sun,6460000.0),earth=skySphere(p,sun,6360000.0);
        vec2 lightOptical=vec2(0);float ls=max(sunlight.y,0.0)/4.0;
        if(earth.x<=0.0){
            for(int j=0;j<4;++j){float h=max(length(p+sun*((float(j)+.5)*ls))-6360000.0,0.0);lightOptical+=exp(-h/vec2(8000,1200))*ls;}
            vec3 tr=exp(-betaR*(optical.x+lightOptical.x)-betaM*1.1*(optical.y+lightOptical.y));
            sumR+=tr*(den.x*ds);sumM+=tr*(den.y*ds);
        }
        optical+=den*ds*.5;
    }
    float mu=dot(dir,sun);
    vec3 col=air.z*(sumR*betaR*(.0596831*(1.0+mu*mu))+sumM*betaM*skyPhase(mu,.76));
    if(ground.x<=0.0){
        float disk=smoothstep(cos(.0049),cos(.0044),mu);
        col+=air.z*disk*exp(-betaR*optical.x-betaM*1.1*optical.y);
    }
    return col;
}
)";
inline const char* body=R"(
    vec2 uv=UV(s);vec4 center=COLOR(uv);float z=DEPTH(uv);
    vec4 nh=invVP*vec4(s*2.0-1.0,1.0-px.w,1.0);
    vec4 fh=invVP*vec4(s*2.0-1.0,min(z,.99999)*px.w+1.0-px.w,1.0);
    vec3 origin=nh.xyz/nh.w,endpoint=fh.xyz/fh.w;
    vec3 direction=normalize(endpoint-origin);
    float reach=z<.99999?length(endpoint-origin)*world.x:50000.0;
    vec3 o=origin*world.x-vec3(0,world.y,0);
    vec3 sd=sun.xyz;
    vec3 solar=sunColor.rgb*skySunTransmission(max(o.y,layer.x),sd,air);
    float sampleOffset=.2+.6*skyHash(vec3(floor(s/px.xy),0));
    vec3 background=z>=.99999?skyAtmosphere(o,direction,sd,air,budget.x)*sunColor.rgb:center.rgb;
    // Separate near-air interval gives shafts enough samples below a distant cloud deck.
    vec3 rays=vec3(0);float airTransmit=1.0;
    if(options.x>0.5&&weather.w>0.0){
        float rayReach=min(reach,options.y),ds=rayReach/budget.w;
        float rayCloudTransmit=1.0;
        float phase=skyPhase(dot(direction,sd),.65);
        for(int i=0;i<32;++i){if(float(i)>=budget.w)break;
            vec3 p=o+direction*((float(i)+.5)*ds);
            float visibility=skyCloudSun(p,sd,layer,weather,budget.z);
            if(options.z>0.5){
                vec3 wp=(p+vec3(0,world.y,0))/world.x;
                vec4 lp=lightVP*vec4(wp,1);vec3 q=lp.xyz/lp.w;
                vec2 suv=UV(q.xy*.5+.5);float sz=q.z/px.w+(px.w-1.0)/px.w;
                if(min(suv.x,suv.y)>0.0&&max(suv.x,suv.y)<1.0&&sz>0.0&&sz<1.0)visibility*=SHADOW(suv,sz);
            }
            float cloudTr=exp(-skyDensity(p,layer,weather)*weather.z*ds);
            rayCloudTransmit*=sqrt(cloudTr);
            float tr=exp(-weather.w*ds);
            rays+=rayCloudTransmit*airTransmit*(1.0-tr)*visibility*phase*air.z*solar;
            rayCloudTransmit*=sqrt(cloudTr);
            airTransmit*=tr;
        }
    }
    vec2 interval=skyLayer(o,direction,layer,reach);
    vec3 cloud=vec3(0);float transmit=1.0;
    vec3 cloudAmbient=skyAtmosphere(vec3(0,layer.x,0),vec3(0,1,0),sd,air,budget.x)*sunColor.rgb*.65;
    if(interval.y>interval.x&&layer.w>0.0){
        float ds=(interval.y-interval.x)/budget.y;
        for(int i=0;i<96;++i){if(float(i)>=budget.y||transmit<.005)break;
            vec3 p=o+direction*(interval.x+(float(i)+sampleOffset)*ds);
            float density=skyDensity(p,layer,weather);
            if(density<=.001)continue;
            float tr=exp(-density*weather.z*ds);
            float sunlight=skyCloudSun(p,sd,layer,weather,budget.z);
            float phase=.85*skyPhase(dot(direction,sd),sun.w)+.15*skyPhase(dot(direction,sd),-.2);
            vec3 illumination=solar*air.z*sunlight*phase+cloudAmbient;
            cloud+=transmit*(1.0-tr)*illumination;
            transmit*=tr;
        }
    }
    // Rays below clouds remain visible; cloud opacity suppresses background radiance.
    vec3 result=(background*transmit+cloud)*airTransmit+rays;
    RETURN(vec4(max(result,vec3(0)),center.a));
)";
}
