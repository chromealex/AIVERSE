#pragma once
// Shared dielectric GGX evaluation; same no-1/pi lighting units as ModelShaders.
#define AIVERSE_GLSL_TERRAIN_PBR \
"\n" \
"vec3 terrainDielectricPbr(vec3 base, vec3 n, vec3 L, vec3 V, vec3 sun, vec3 ambient, float shadow, float roughness, float ao) {\n" \
"    float r=clamp(roughness,0.045,1.0), nl=max(dot(n,L),0.0), nv=max(dot(n,V),0.0001);\n" \
"    vec3 H=normalize(L+V);float nh=max(dot(n,H),0.0);\n" \
"    float a=r*r, a2=a*a, d=nh*nh*(a2-1.0)+1.0;\n" \
"    float D=a2/(3.14159265*d*d);\n" \
"    float k=(r+1.0)*(r+1.0)*0.125;\n" \
"    float visibility=0.25/((nl*(1.0-k)+k)*(nv*(1.0-k)+k));\n" \
"    vec3 F=vec3(0.04)+vec3(0.96)*pow(1.0-max(dot(L,H),0.0),5.0);\n" \
"    vec3 spec=D*visibility*F*nl*3.14159265;\n" \
"    return base*ambient*ao*0.96 + sun*shadow*((vec3(1.0)-F)*base*nl+spec);\n" \
"}\n"
#define AIVERSE_MSL_TERRAIN_PBR \
"\n" \
"float3 terrainDielectricPbr(float3 base, float3 n, float3 L, float3 V, float3 sun, float3 ambient, float shadow, float roughness, float ao) {\n" \
"    float r=clamp(roughness,0.045,1.0), nl=max(dot(n,L),0.0), nv=max(dot(n,V),0.0001);\n" \
"    float3 H=normalize(L+V);float nh=max(dot(n,H),0.0);\n" \
"    float a=r*r, a2=a*a, d=nh*nh*(a2-1.0)+1.0;\n" \
"    float D=a2/(3.14159265*d*d);\n" \
"    float k=(r+1.0)*(r+1.0)*0.125;\n" \
"    float visibility=0.25/((nl*(1.0-k)+k)*(nv*(1.0-k)+k));\n" \
"    float3 F=float3(0.04)+float3(0.96)*pow(1.0-max(dot(L,H),0.0),5.0);\n" \
"    float3 spec=D*visibility*F*nl*3.14159265;\n" \
"    return base*ambient*ao*0.96 + sun*shadow*((float3(1.0)-F)*base*nl+spec);\n" \
"}\n"
