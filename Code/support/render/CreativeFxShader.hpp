#pragma once

namespace fml::fx {
// Included by FxShaders.cpp. No state/history: the requested time fully defines
// the image. Parameters remain continuous except for explicit counts (sectors,
// posterization levels). Pixel radii use the document's scale in both outputs.
inline const char* creativeShaderBody() { return R"GLSL(
in vec3 vUV;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec2 uResolution;
uniform float uCreativeMix, uCreativeTime, uCreativeScale;
uniform float uCP0, uCP1, uCP2, uCP3, uCP4, uCP5;
uniform float uCD0,uCD1,uCD2,uCD3,uCD4,uCD5,uCreativeVariant;
uniform vec3 uCreativeColor;
const float PI=3.14159265359;
vec4 source(vec2 uv) { return texture(uTexture,clamp(uv,vec2(0),vec2(1))); }
float luma(vec3 c) { return dot(c,vec3(.2126,.7152,.0722)); }
mat2 rotation(float a) { float c=cos(a),s=sin(a); return mat2(c,s,-s,c); }
vec2 documentUV(vec2 uv) { return vec2(uv.x,1.-uv.y); }
float classicHash(vec2 p) {p=fract(p*vec2(123.34,456.21));p+=dot(p,p+45.32);return fract(p.x*p.y);}
vec3 straight(vec4 c) {return c.a>.00001 ? c.rgb/c.a : vec3(0);}
void main() {
    vec2 uv=vUV.xy/vUV.z;
    vec2 px=vec2(max(.001,uCreativeScale))/max(uResolution,vec2(1));
    float aspect=uResolution.x/max(1.,uResolution.y);
    vec4 original=source(uv), base=original;
#if FX_TYPE >= 7 && FX_TYPE <= 11
    // Framebuffers are premultiplied. Grade straight colour, then restore
    // coverage; a duotone must not turn the transparent canvas into a rectangle.
    base.rgb=base.a>.00001 ? base.rgb/base.a : vec3(0);
#endif
    vec4 result=base;
#if FX_TYPE == 1
    // Soft-threshold light extraction, then a bounded 7x7 Gaussian kernel.
    vec3 light=vec3(0); float weight=0.;
    for(int y=-3;y<=3;++y) for(int x=-3;x<=3;++x) {
        vec2 q=vec2(x,y)/3.;
        float w=exp(-dot(q,q)*2.5);
        vec4 tap=source(uv+q*px*uCP1);
        float b=max(tap.r,max(tap.g,tap.b));
        float knee=max(.001,uCP3);
        float soft=clamp((b-uCP2+knee)/(2.*knee),0.,1.);
        float contribution=max(b-uCP2,soft*soft*knee)/max(.0001,b);
        light+=tap.rgb*contribution*w; weight+=w;
    }
    vec3 halo=light/max(.001,weight)*uCP0*uCreativeColor;
    result.rgb=base.rgb+halo;
    result.a=max(base.a,clamp(max(halo.r,max(halo.g,halo.b)),0.,1.));
#elif FX_TYPE == 2
    vec2 centre=vec2(uCP1,1.-uCP2);
    vec2 ray=uv-centre;
    vec4 sum=vec4(0); float weight=0.;
    for(int i=-12;i<=12;++i) {
        float t=float(i)/12.; float w=exp(-2.5*t*t);
        sum+=source(uv+ray*(t+uCP3)*uCP0)*w; weight+=w;
    }
    result=sum/weight;
#elif FX_TYPE == 3
    float a=radians(uCP1); vec2 dir=vec2(cos(a),-sin(a))*px*uCP0;
    vec4 sum=vec4(0); float weight=0.;
    for(int i=-12;i<=12;++i) {
        float t=float(i)/12.; float w=exp(-2.5*t*t);
        sum+=source(uv+dir*(t+uCP2))*w; weight+=w;
    }
    result=sum/weight;
#elif FX_TYPE == 4
    vec2 centre=vec2(uCP3,1.-uCP4), p=(uv-centre)*vec2(aspect,1);
    float r=length(p), radius=uCP0+uCreativeTime*uCP5;
    float d=(r-radius)/max(.001,uCP1);
    float wave=sin(d*PI)*exp(-d*d*3.);
    vec2 dir=r>.00001 ? p/r/vec2(aspect,1) : vec2(0);
    result=source(uv+dir*wave*uCP2*px.y);
#elif FX_TYPE == 5
    vec2 p=documentUV(uv)*vec2(aspect,1)*uCP1;
    float t=uCreativeTime;
    vec2 wave=vec2(sin(p.y+t)+uCP2*sin(p.x*1.73-t*.71),
                   cos(p.x-t*.87)+uCP2*sin(p.y*1.91+t*.59));
    result=source(uv+wave*vec2(uCP3,-uCP4)*px*uCP0);
#elif FX_TYPE == 6
    vec2 centre=vec2(uCP3,1.-uCP4), p=(uv-centre)*vec2(aspect,1);
    float r=length(p)/max(.01,uCP2), wedge=2.*PI/max(2.,round(uCP0));
    float a=atan(p.y,p.x)+radians(uCP1);
    a=abs(mod(a+wedge*.5,wedge)-wedge*.5);
    vec2 q=vec2(cos(a),sin(a))*r/vec2(aspect,1)+centre;
    // Mirror-repeat rather than smear the last row when the lens samples past an edge.
    result=source(1.-abs(mod(q,2.)-1.));
#elif FX_TYPE == 7
    float levels=max(2.,round(uCP0))-1.;
    vec2 cell=floor(gl_FragCoord.xy/max(.001,uCreativeScale));
    float d=fract(dot(cell,vec2(.754877666,.569840296)))-.5;
    result.rgb=floor(clamp(base.rgb+d*uCP1/levels,0.,1.)*levels+.5)/levels;
#elif FX_TYPE == 8
    float v=clamp((luma(base.rgb)-uCP0)*uCP1+.5,0.,1.);
    result.rgb=mix(vec3(uCP2,uCP3,uCP4),uCreativeColor,v);
#elif FX_TYPE == 9
    float spacing=max(1.,uCP0*uCreativeScale);
    mat2 rot=rotation(radians(uCP1));
    vec2 p=rot*(uv*uResolution), cell=floor(p/spacing)+.5;
    vec2 sampleUV=transpose(rot)*(cell*spacing)/uResolution;
    vec3 ink=source(sampleUV).rgb;
    float radius=sqrt(clamp(1.-luma(ink),0.,1.))*.7071;
    float distance=length(fract(p/spacing)-.5);
    float printed=1.-smoothstep(radius-uCP2,radius+uCP2,distance);
    result.rgb=mix(vec3(1),mix(vec3(0),ink,clamp(uCP3,0.,1.)),printed);
#elif FX_TYPE == 10
    vec3 linear=max(vec3(0),base.rgb)*exp2(uCP0);
    linear=mix(linear,linear/(1.+linear),clamp(uCP2,0.,1.));
    result.rgb=pow(linear,vec3(1./max(.01,uCP1)));
#elif FX_TYPE == 11
    float a=radians(uCP1); vec2 dir=vec2(cos(a),-sin(a))*px*uCP2;
    float relief=(luma(source(uv+dir).rgb)-luma(source(uv-dir).rgb))*uCP0+.5;
    result.rgb=mix(vec3(relief),base.rgb*relief*2.,clamp(uCP3,0.,1.));
#elif FX_TYPE == 12
    vec2 centre=vec2(uCP2,1.-uCP3), p=(uv-centre)*vec2(aspect,1);
    float fall=pow(clamp(1.-length(p)/max(.001,uCP1),0.,1.),max(.01,uCP4));
    result=source(rotation(radians(uCP0)*fall)*p/vec2(aspect,1)+centre);
 #elif FX_TYPE == 13
    vec2 pivot=vec2(uCP4,uCP5)*uResolution;
    vec2 destination=documentUV(uv)*uResolution;
    vec2 q=rotation(-uCP3)*(destination-pivot-vec2(uCP0,uCP1)*uCreativeScale)/max(.001,uCP2)+pivot;
    q/=uResolution;
    result=any(lessThan(q,vec2(0))) || any(greaterThan(q,vec2(1))) ? vec4(0) : source(documentUV(q));
    result*=clamp(uCreativeColor.r,0.,1.);
#elif FX_TYPE == 14
    int kind=int(round(uCreativeVariant));
    vec3 rgb=straight(base);float strength=max(0.,uCP0);
    if(kind==10) {
        // Film-like grain affects midtones most, with independent colour noise.
        vec2 cell=floor(documentUV(uv)*vec2(uCD4,uCD5)/max(1.,uCP1));
        float tick=floor(uCreativeTime*24.);
        float mono=classicHash(cell+tick)-.5;
        vec3 noise=mix(vec3(mono),vec3(mono,classicHash(cell+tick+17.)-.5,classicHash(cell+tick+59.)-.5),uCD1);
        float protect=mix(1.,clamp(4.*luma(rgb)*(1.-luma(rgb)),.1,1.),uCD2);
        result.rgb=clamp(rgb+noise*strength*uCD3*protect,0.,1.)*base.a;
    } else if(kind==11) {
        vec3 grade=rgb*exp2(uCD1);
        grade*=vec3(1.+uCD2*.25+uCD3*.08,1.-uCD3*.16,1.-uCD2*.25+uCD3*.08);
        grade=mix(vec3(luma(grade)),grade,max(0.,uCP1));
        grade=(grade-.5)*max(0.,uCP2)+.5;
        grade*=uCreativeColor;
        result.rgb=mix(rgb,clamp(grade,0.,1.),clamp(strength,0.,1.))*base.a;
    } else if(kind==12) {
        vec2 p=(documentUV(uv)-.5-vec2(uCP4,uCP5)/vec2(uCD4,uCD5))*2.*vec2(uCP2,uCP3);
        float distance=mix(max(abs(p.x),abs(p.y)),length(p),uCD2);
        float shade=smoothstep(uCD1,uCD1+max(.01,uCP1),distance)*clamp(strength,0.,1.);
        result.rgb=mix(rgb,rgb*mix(vec3(0),uCreativeColor,uCD3),shade)*base.a;
    } else if(kind==13) {
        vec2 p=(uv-.5)*vec2(aspect,1);float radial=pow(clamp(length(p),0.,1.),max(.1,uCD2));
        vec2 dir=mix(vec2(uCP1,-uCP2),normalize(p+vec2(.000001))*radial,uCD1);
        dir=rotation(radians(uCD3))*dir;
        vec4 r=source(uv+dir*px*strength),b=source(uv-dir*px*strength);
        result=vec4(r.r,base.g,b.b,max(base.a,max(r.a,b.a)));
    } else if(kind==14) {
        vec2 p=(uv-.5)*2.,q=p*(1.+dot(p,p)*uCD1)*.5+.5;
        vec4 image=any(lessThan(q,vec2(0))) || any(greaterThan(q,vec2(1))) ? vec4(0) : source(q);
        vec2 coord=documentUV(q)*vec2(uCD4,uCD5)+vec2(uCP4,uCP5);
        vec2 waves=.5+.5*cos(coord/max(1.,uCP1)*6.2831853);
        vec2 lines=smoothstep(vec2(.5-uCD3*.5),vec2(.5+uCD3*.5),waves);
        float mask=1.-clamp(strength,0.,1.)*clamp(lines.y*uCP2+lines.x*uCP3,0.,1.);
        float channel=mod(floor(coord.x),3.);vec3 phosphor=mix(vec3(1),vec3(channel<.5 ? 1. : .65,channel>.5 && channel<1.5 ? 1. : .65,channel>1.5 ? 1. : .65),uCD2);
        result=image*vec4(phosphor*mask,1);
    } else if(kind==15) {
        float tick=floor(uCreativeTime),fraction=fract(uCreativeTime),envelope=mix(1.,sin(fraction*PI),uCD3);
        vec2 band=floor(documentUV(uv)*max(1.,uCP1));
        vec2 noise=vec2(classicHash(vec2(band.y,tick)),classicHash(vec2(band.x+19.,tick)));
        vec2 gate=step(vec2(1.-uCD1),noise);
        vec2 shift=(noise-.5)*gate*vec2(uCP2,uCP3)*strength*.18*envelope;
        vec4 moved=source(uv+shift),r=source(uv+shift*(1.+uCD2)),b=source(uv+shift*(1.-uCD2));
        result=vec4(r.r,moved.g,b.b,max(moved.a,max(r.a,b.a)));
    } else if(kind==16) {
        vec2 block=max(vec2(1),vec2(uCP1,uCP2)*max(1.,round(strength)));
        vec2 doc=documentUV(uv)*vec2(uCD4,uCD5),offset=vec2(uCP3,uCP4);
        vec2 centre=(floor((doc+offset)/block)+.5)*block-offset;
        vec4 average=vec4(0);
        for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x) average+=source(documentUV((centre+vec2(x,y)*block/3.)/vec2(uCD4,uCD5)))/9.;
        result=mix(source(documentUV(centre/vec2(uCD4,uCD5))),average,uCD1);
        float levels=max(2.,uCD2)-1.;vec3 c=straight(result);
        float d=(classicHash(floor(centre))-.5)*uCD3/levels;
        result.rgb=floor(clamp(c+d,0.,1.)*levels+.5)/levels*result.a;
        if(strength<=1.) result=original;
    } else if(kind==17) {
        vec2 p=rotation(radians(uCD2))*((uv-.5)*vec2(aspect,1));p=p/vec2(aspect,1)+.5;
        float sign=uCD1>.5 ? 1. : -1.;
        if(uCP1<.5 || uCP1>1.5) p.x=uCP2+sign*abs(p.x-uCP2);
        if(uCP1>.5) p.y=uCP2+sign*abs(p.y-uCP2);
        p=rotation(-radians(uCD2))*((p-.5)*vec2(aspect,1))/vec2(aspect,1)+.5;
        if(uCD3>.5) p=1.-abs(mod(p,2.)-1.);
        result=mix(base,source(p),clamp(strength,0.,1.));
    }
    if(strength<=0.) result=original;
#elif FX_TYPE == 16
    // PERSPECTIVA. El plano fuente vive en z = f delante de una camara en el
    // origen; se gira alrededor del pivote y cada pixel de salida se lleva de
    // vuelta al plano resolviendo el rayo contra el plano girado (Cramer con
    // productos mixtos). Sin giro, la aplicacion es exactamente la identidad.
    float f=max(.05,uCP3);
    vec2 pivot=vec2(uCP4,uCP5);
    vec2 p=(documentUV(uv)-pivot)*vec2(aspect,1);
    float ax=radians(uCP0),ay=radians(uCP1),az=radians(uCP2);
    float cx=cos(ax),sx=sin(ax),cy=cos(ay),sy=sin(ay),cz=cos(az),sz=sin(az);
    mat3 rx=mat3(1,0,0, 0,cx,sx, 0,-sx,cx);
    mat3 ry=mat3(cy,0,-sy, 0,1,0, sy,0,cy);
    mat3 rz=mat3(cz,sz,0, -sz,cz,0, 0,0,1);
    mat3 rot=rz*ry*rx;
    vec3 a=rot[0],b=rot[1],c=-vec3(p,f),rhs=vec3(0,0,-f);
    float det=dot(a,cross(b,c));
    if(abs(det)<1e-6) result=vec4(0);
    else {
        float u=dot(rhs,cross(b,c))/det;
        float v=dot(a,cross(rhs,c))/det;
        float t=dot(a,cross(b,rhs))/det;
        vec2 q=pivot+vec2(u,v)/vec2(aspect,1);
        // Dentro, cobertura entera hasta el ultimo pixel -sin giro tiene que
        // ser la identidad exacta-; fuera, medio pixel de difuminado.
        vec2 edge=min(q,1.-q)*uResolution;
        float cover=t>0. ? clamp(min(edge.x,edge.y)+1.,0.,1.) : 0.;
        result=cover>0. ? source(documentUV(q))*cover : vec4(0);
    }
#endif
#if FX_TYPE >= 7 && FX_TYPE <= 11
    result.rgb=clamp(result.rgb,0.,1.)*result.a;
#endif
    FragColor=mix(original,vec4(clamp(result.rgb,vec3(0),vec3(result.a)),result.a),clamp(uCreativeMix,0.,1.))*vColor;
}
)GLSL"; }
}
