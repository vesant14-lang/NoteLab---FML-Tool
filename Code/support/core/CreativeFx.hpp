#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace fml::fx {

// Stable append-only IDs, independent of the editor and of the rendering API.
enum class CreativeType {
    None, Bloom, RadialBlur, DirectionalBlur, Shockwave, Liquid,
    Kaleidoscope, Posterize, Duotone, Halftone, Exposure, Emboss, Swirl, Affine,
    ClassicFinish, TextShape,
    // Se anade al final: el numero entra en el nombre del programa de shader.
    Perspective
};
struct CreativeParam {
    const char* name = nullptr;
    float initial = 0, min = 0, max = 1, step = .01f;
    const char* format = "%.2f";
    bool basic = false;
};
struct CreativeSpec {
    CreativeType type;
    const char* id;
    const char* name;
    const char* category;
    const char* detail;
    std::array<CreativeParam, 6> params;
    const char* colorLabel = nullptr;
    bool animated = false;
};
inline const std::vector<CreativeSpec>& creativeCatalog() {
    static const std::vector<CreativeSpec> list = {
        {CreativeType::Bloom,"bloom-threshold","Bloom","Light",
         "Extracts bright areas and spreads their light. Radius, threshold and knee are independent.",{{
            {"Light strength",1.2f,0,5,.01f,"%.2fx",true}, {"Radius",16,1,120,.2f,"%.1f px",true},
            {"Threshold",.65f,0,1,.005f}, {"Soft knee",.25f,.01f,1,.005f}}},"Light colour"},
        {CreativeType::RadialBlur,"radial-blur","Radial Blur","Focus",
         "Zoom streaks toward an editable centre. Does not require previous frames.",{{
            {"Reach",.08f,0,.5f,.002f,"%.3f",true}, {"Centre X",.5f,0,1,.005f},
            {"Centre Y",.5f,0,1,.005f}, {"Bias",0,-1,1,.01f}}}},
        {CreativeType::DirectionalBlur,"directional-blur","Directional Blur","Focus",
         "Weighted streaks at any angle, with an adjustable leading or trailing bias.",{{
            {"Distance",18,0,160,.2f,"%.1f px",true}, {"Angle",25,-180,180,.5f,"%.1f deg",true},
            {"Bias",0,-1,1,.01f}}}},
        {CreativeType::Shockwave,"shockwave","Shockwave","Distort",
         "An expanding ring distorts the actual image. Key Radius or enable Travel for motion.",{{
            {"Radius",.3f,0,2,.005f,"%.3f",true}, {"Width",.12f,.005f,.8f,.005f},
            {"Displacement",18,-100,100,.2f,"%.1f px",true}, {"Centre X",.5f,0,1,.005f},
            {"Centre Y",.5f,0,1,.005f}, {"Travel",0,0,2,.01f,"radii/s"}}}},
        {CreativeType::Liquid,"liquid-distortion","Liquid Distortion","Distort",
         "Smooth two-axis refraction driven by timeline time, not playback history.",{{
            {"Displacement",12,0,100,.2f,"%.1f px",true}, {"Wave frequency",5,.1f,30,.1f,"%.1f",true},
            {"Detail",.35f,0,1,.01f}, {"X influence",1,0,2,.01f}, {"Y influence",1,0,2,.01f}}},nullptr,true},
        {CreativeType::Kaleidoscope,"kaleidoscope","Kaleidoscope","Distort",
         "Repeats mirrored angular sectors, with rotation, scale and a movable centre.",{{
            {"Sectors",6,2,24,1,"%.0f",true}, {"Rotation",0,-180,180,.5f,"%.1f deg",true},
            {"Scale",1,.1f,4,.01f}, {"Centre X",.5f,0,1,.005f}, {"Centre Y",.5f,0,1,.005f}}}},
        {CreativeType::Posterize,"posterize","Posterize","Finish",
         "Reduces colour levels; optional ordered dithering softens banding without random flicker.",{{
            {"Levels",5,2,32,1,"%.0f",true}, {"Dither",.25f,0,1,.01f,"%.2f",true}}}},
        {CreativeType::Duotone,"duotone","Duotone","Finish",
         "Maps luminance between a shadow colour and a highlight colour, with editable balance.",{{
            {"Balance",.5f,.05f,.95f,.005f,"%.2f",true}, {"Contrast",1,.1f,3,.01f},
            {"Shadow red",.04f,0,1,.005f}, {"Shadow green",.02f,0,1,.005f},
            {"Shadow blue",.12f,0,1,.005f}}},"Highlight colour"},
        {CreativeType::Halftone,"halftone","Print Halftone","Finish",
         "Luminance controls the area of print dots on a rotated screen, with optional colour ink.",{{
            {"Dot spacing",7,2,40,.1f,"%.1f px",true}, {"Angle",15,-90,90,.5f,"%.1f deg",true},
            {"Soft edge",.08f,.01f,.3f,.005f}, {"Colour ink",0,0,1,.01f}}}},
        {CreativeType::Exposure,"exposure-gamma","Exposure / Gamma","Finish",
         "Exposure in stops, gamma and a soft highlight shoulder, blended with the original.",{{
            {"Exposure",.7f,-5,5,.01f,"%+.2f stops",true}, {"Gamma",1.1f,.2f,4,.01f,"%.2f",true},
            {"Highlight shoulder",.15f,0,1,.005f}}}},
        {CreativeType::Emboss,"emboss","Emboss","Light",
         "Directional relief from image luminance, with depth and original-colour retention.",{{
            {"Depth",2,0,10,.05f,"%.2f",true}, {"Light angle",45,-180,180,.5f,"%.1f deg",true},
            {"Radius",2,.5f,12,.1f,"%.1f px"}, {"Keep colour",.25f,0,1,.01f}}}},
        {CreativeType::Swirl,"swirl-lens","Swirl Lens","Distort",
         "A local lens twists the scene smoothly inside its radius, leaving the exterior unchanged.",{{
            {"Twist",60,-360,360,.5f,"%.1f deg",true}, {"Radius",.65f,.05f,2,.005f,"%.2f",true},
            {"Centre X",.5f,0,1,.005f}, {"Centre Y",.5f,0,1,.005f}, {"Falloff",2,.2f,6,.01f}}}},
        // 2.5D: el cuadro como un plano inclinado en el espacio. No es una
        // camara 3D ni un lienzo infinito: lo que sale del plano es
        // transparente, y el pivote es un punto del lienzo.
        {CreativeType::Perspective,"perspective-3d","Perspective 3D","Distort",
         "Tilts the WHOLE composed frame as one flat plane. For real depth use the 3D section of each layer (tilt, depth) and the 3D Camera effect on MASTER.",{{
            {"Tilt X",25,-80,80,.5f,"%.1f deg",true}, {"Tilt Y",-20,-80,80,.5f,"%.1f deg",true},
            {"Turn",0,-180,180,.5f,"%.1f deg",true}, {"Distance",2,.6f,6,.01f,"%.2f",true},
            {"Centre X",.5f,0,1,.005f}, {"Centre Y",.5f,0,1,.005f}}}}
    };
    return list;
}
struct CreativePass {
    CreativeType type = CreativeType::None;
    float mix = 0;
    std::array<float,6> params{};
    std::array<float,6> detail{};
    float variant=0;
    std::array<float,3> color{{1,1,1}};
    float time = 0;
    float pixelScale = 1;
    bool active() const { return type != CreativeType::None && std::isfinite(mix) && mix > .00001f; }
};
inline CreativePass creativeDefaults(const CreativeSpec& spec) {
    CreativePass p; p.type=spec.type; p.mix=1;
    for (int i=0;i<6;++i) p.params[i]=spec.params[i].initial;
    if (spec.type==CreativeType::Duotone) p.color={{1,.75f,.35f}};
    return p;
}
} // namespace fml::fx
