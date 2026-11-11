#pragma once
#include "ShaderGraph.h"
#include <functional>
#include <stdexcept>

namespace aiverse::render::graph {
// Expressions use only compiler-owned identifiers; never interpolate project source text.
inline std::string nodeExpression(const Json& n,const std::function<std::string(size_t)>& a,bool metal,
                                  const std::string& id,std::string& code) {
    const std::string op=n.getStr("op"), v=metal?"float4":"vec4", v3=metal?"float3":"vec3", v2=metal?"float2":"vec2";
    auto wrap=[&](const std::string& x){return v+"("+x+")";};
    auto safe=[&](const std::string& x){return "(mix("+v+"(-1.0),"+v+"(1.0),step("+v+"(0.0),"+x+"))*max(abs("+x+"),"+v+"(0.00001)))";};
    auto norm=[&](const std::string& x){return "("+x+"/max(length("+x+"),0.00001))";};
    if(op=="uv")return wrap("sg_uv,0.0,1.0");
    if(op=="input")return "sg_input";
    if(op=="time")return wrap("sg_time");
    if(op=="worldPosition")return wrap("sg_world,1.0");
    if(op=="objectPosition")return wrap("sg_object,1.0");
    if(op=="normal")return wrap(norm("sg_normal")+",0.0");
    if(op=="viewDirection")return wrap(norm("sg_view")+",0.0");
    if(op=="vertexColor")return "sg_color";
    if(op=="screenPosition")return wrap("sg_screen,0.0,1.0");
    if(op=="particleAge")return wrap("sg_particle.x");
    if(op=="particleLifetime")return wrap("sg_particle.y");
    if(op=="particleAge01")return wrap("clamp(sg_particle.x/max(sg_particle.y,0.00001),0.0,1.0)");
    if(op=="particleRandom")return wrap("sg_particle.z");
    if(op=="add"||op=="subtract"||op=="multiply")return a(0)+(op=="add"?" + ":op=="subtract"?" - ":" * ")+a(1);
    if(op=="divide")return a(0)+"/"+safe(a(1));
    if(op=="reciprocal")return v+"(1.0)/"+safe(a(0));
    if(op=="mix")return "mix("+a(0)+","+a(1)+","+a(2)+")";
    if(op=="sin"||op=="cos"||op=="atan"||op=="abs"||op=="sign"||op=="floor"||op=="ceil"||op=="fract")return op+"("+a(0)+")";
    if(op=="round")return "floor("+a(0)+"+"+v+"(0.5))";
    if(op=="truncate")return "sign("+a(0)+")*floor(abs("+a(0)+"))";
    if(op=="negate")return "-"+a(0);
    if(op=="tan")return "sin("+a(0)+")/"+safe("cos("+a(0)+")");
    if(op=="asin"||op=="acos")return op+"(clamp("+a(0)+","+v+"(-1.0),"+v+"(1.0)))";
    if(op=="atan2")return std::string(metal?"atan2":"atan")+"("+a(0)+","+safe(a(1))+")";
    if(op=="radians"||op=="degrees")return a(0)+(op=="radians"?"*0.01745329252":"*57.2957795131");
    if(op=="exp"||op=="exp2")return op+"(clamp("+a(0)+","+v+"(-80.0),"+v+"(80.0)))";
    if(op=="log"||op=="log2")return op+"(max("+a(0)+","+v+"(0.00001)))";
    if(op=="sqrt")return "sqrt(max("+a(0)+","+v+"(0.0)))";
    if(op=="inverseSqrt")return v+"(1.0)/sqrt(max("+a(0)+","+v+"(0.00001)))";
    if(op=="pow")return "exp(clamp(log(max(abs("+a(0)+"),"+v+"(0.00001)))*"+a(1)+","+v+"(-80.0),"+v+"(80.0)))";
    if(op=="min"||op=="max"||op=="step")return op+"("+a(0)+","+a(1)+")";
    if(op=="modulo")return a(0)+"-"+safe(a(1))+"*floor("+a(0)+"/"+safe(a(1))+")";
    if(op=="clamp")return "clamp("+a(0)+","+v+"(0.0),"+v+"(1.0))";
    if(op=="clampRange")return "clamp("+a(0)+",min("+a(1)+","+a(2)+"),max("+a(1)+","+a(2)+"))";
    if(op=="smoothstep") {
        code+=v+" "+id+"_t=clamp(("+a(2)+"-"+a(0)+")/"+safe("("+a(1)+"-"+a(0)+")")+","+v+"(0.0),"+v+"(1.0));\n";
        return id+"_t*"+id+"_t*("+v+"(3.0)-2.0*"+id+"_t)";
    }
    if(op=="inverseLerp")return "("+a(2)+"-"+a(0)+")/"+safe("("+a(1)+"-"+a(0)+")");
    if(op=="remap")return a(3)+"+("+a(0)+"-"+a(1)+")/"+safe("("+a(2)+"-"+a(1)+")")+"*("+a(4)+"-"+a(3)+")";
    if(op=="oneMinus")return v+"(1.0)-"+a(0);
    if(op=="normalize")return norm(a(0));
    if(op=="normalize3")return wrap(norm(a(0)+".xyz")+",0.0");
    if(op=="dot"||op=="length"||op=="distance")return wrap(op+"("+a(0)+(op=="length"?"":","+a(1))+")");
    if(op=="dot3"||op=="length3"||op=="distance3")return wrap(op.substr(0,op.size()-1)+"("+a(0)+".xyz"+(op=="length3"?"":","+a(1)+".xyz")+")");
    if(op=="cross"||op=="reflect")return wrap(op+"("+a(0)+".xyz,"+(op=="reflect"?norm(a(1)+".xyz"):a(1)+".xyz")+"),0.0");
    if(op=="refract")return wrap("refract("+norm(a(0)+".xyz")+","+norm(a(1)+".xyz")+","+a(2)+".r),0.0");
    if(op=="fresnel")return wrap("pow(max(1.0-clamp(dot("+norm(a(0)+".xyz")+","+norm(a(1)+".xyz")+"),0.0,1.0),0.00001),max("+a(2)+".r,0.0))");
    if(op=="red"||op=="green"||op=="blue"||op=="alpha")return wrap(a(0)+"."+(op=="red"?"r":op=="green"?"g":op=="blue"?"b":"a"));
    if(op=="rgb")return wrap(a(0)+".rgb,1.0");
    if(op=="setAlpha")return wrap(a(0)+".rgb,"+a(1)+".r");
    if(op=="combine")return wrap(a(0)+".r,"+a(1)+".r,"+a(2)+".r,"+a(3)+".r");
    if(op=="append")return wrap(a(0)+".xy,"+a(1)+".xy");
    if(op=="swizzle") {
        const auto mask=n.getStr("mask","rgba");
        if(mask.size()!=4||mask.find_first_not_of("rgba")!=std::string::npos)throw std::runtime_error("shader graph: swizzle mask must contain four rgba letters");
        return a(0)+"."+mask;
    }
    if(op=="tilingOffset")return wrap(a(0)+".xy*"+a(1)+".xy+"+a(2)+".xy,0.0,1.0");
    if(op=="panner")return wrap(a(0)+".xy+"+a(1)+".xy*"+a(2)+".r,0.0,1.0");
    if(op=="rotateUV") {
        code+=v2+" "+id+"_p="+a(0)+".xy-"+a(1)+".xy; float "+id+"_c=cos("+a(2)+".r),"+id+"_s=sin("+a(2)+".r);\n";
        return wrap(v2+"("+id+"_c*"+id+"_p.x-"+id+"_s*"+id+"_p.y,"+id+"_s*"+id+"_p.x+"+id+"_c*"+id+"_p.y)+"+a(1)+".xy,0.0,1.0");
    }
    if(op=="polarUV")return wrap("length("+a(0)+".xy-"+a(1)+".xy),"+std::string(metal?"atan2":"atan")+"("+a(0)+".y-"+a(1)+".y,"+a(0)+".x-"+a(1)+".x+0.000001)/6.28318530718+0.5,0.0,1.0");
    if(op=="flipbook") {
        code+="float "+id+"_c=max(floor("+a(1)+".r),1.0),"+id+"_r=max(floor("+a(2)+".r),1.0); float "+id+"_f=floor(max("+a(3)+".r,0.0)); "+id+"_f-=floor("+id+"_f/("+id+"_c*"+id+"_r))*"+id+"_c*"+id+"_r;\n";
        return wrap("("+a(0)+".xy+"+v2+"("+id+"_f-floor("+id+"_f/"+id+"_c)*"+id+"_c,floor("+id+"_f/"+id+"_c)))/"+v2+"("+id+"_c,"+id+"_r),0.0,1.0");
    }
    if(op=="checkerboard")return wrap("abs(fract((floor("+a(0)+".x)+floor("+a(0)+".y))*0.5)*2.0)");
    if(op=="noise"||op=="gradientNoise"||op=="voronoi") {
        // Same smooth lattice interpolation as FxNoise.h, expressed in portable shader arithmetic.
        code+=v2+" "+id+"_i=floor("+a(0)+".xy),"+id+"_f=fract("+a(0)+".xy);\n";
        if(op=="voronoi") {
            code+="float "+id+"_d=8.0;\nfor(int sy=-1;sy<=1;++sy)for(int sx=-1;sx<=1;++sx){"+v2+" cell="+v2+"(float(sx),float(sy)); "+v2+" q="+id+"_i+cell; "+v2+" h=fract(sin("+v2+"(dot(q,"+v2+"(127.1,311.7)),dot(q,"+v2+"(269.5,183.3))))*43758.5453); "+id+"_d=min("+id+"_d,length(cell+h-"+id+"_f));}\n";
            return wrap(id+"_d");
        }
        for(int k=0;k<4;++k){const auto t=id+"_h"+std::to_string(k);const auto xy=v2+"("+std::to_string(k%2)+".0,"+std::to_string(k/2)+".0)";
            code+="float "+t+"=fract(sin(dot("+id+"_i+"+xy+","+v2+"(127.1,311.7)))*43758.5453);\n";
            if(op=="gradientNoise")code+=t+"=dot("+v2+"(cos("+t+"*6.2831853),sin("+t+"*6.2831853)),"+id+"_f-"+xy+");\n";
        }
        code+=id+"_f="+id+"_f*"+id+"_f*(3.0-2.0*"+id+"_f);\n";
        return wrap("mix(mix("+id+"_h0,"+id+"_h1,"+id+"_f.x),mix("+id+"_h2,"+id+"_h3,"+id+"_f.x),"+id+"_f.y)"+(op=="gradientNoise"?"*0.5+0.5":""));
    }
    const auto luma="dot("+a(0)+".rgb,"+v3+"(0.2126,0.7152,0.0722))";
    if(op=="luminance")return wrap(luma);
    if(op=="desaturate"||op=="saturation")return wrap("mix("+v3+"("+luma+"),"+a(0)+".rgb,"+(op=="desaturate"?"1.0-":"")+a(1)+".r),"+a(0)+".a");
    if(op=="contrast")return wrap("("+a(0)+".rgb-0.5)*"+a(1)+".r+0.5,"+a(0)+".a");
    if(op=="srgbToLinear")return wrap(a(0)+".rgb*("+a(0)+".rgb*("+a(0)+".rgb*0.305306011+0.682171111)+0.012522878),"+a(0)+".a");
    if(op=="linearToSrgb")return wrap("mix("+a(0)+".rgb*12.92,1.055*pow(max("+a(0)+".rgb,"+v3+"(0.0)),"+v3+"(0.416666667))-0.055,step("+v3+"(0.0031308),"+a(0)+".rgb)),"+a(0)+".a");
    if(op=="normalUnpack")return wrap(norm("("+a(0)+".xyz*2.0-1.0)")+",0.0");
    if(op=="normalStrength")return wrap(norm(v3+"("+a(0)+".xy*"+a(1)+".r,"+a(0)+".z)")+",0.0");
    if(op=="lessThan")return v+"(1.0)-step("+a(1)+","+a(0)+")";
    if(op=="greaterThan")return v+"(1.0)-step("+a(0)+","+a(1)+")";
    if(op=="equal")return v+"(1.0)-step("+v+"(0.00001),abs("+a(0)+"-"+a(1)+"))";
    if(op=="branch")return "mix("+a(2)+","+a(1)+",step("+v+"(0.5),"+a(0)+"))";
    if(op=="ddx"||op=="ddy"||op=="fwidth")return std::string(op=="fwidth"?"fwidth":op=="ddx"?(metal?"dfdx":"dFdx"):(metal?"dfdy":"dFdy"))+"("+a(0)+")";
    throw std::runtime_error("shader graph: operation has no expression: "+op);
}
}
