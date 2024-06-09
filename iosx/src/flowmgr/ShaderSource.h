#define MTL_STRINGIFY(s) @ #s

static NSString *const g_ShaderSrc = MTL_STRINGIFY(
    using namespace metal;

    typedef struct {
      packed_float2 position;
      packed_float2 texcoord;
    } Vertex;

    typedef struct {
      float4 position[[position]];
      float2 texcoord;
    } Varyings;

    vertex Varyings vertexPassthrough(constant Vertex *verticies[[buffer(0)]],
                                      unsigned int vid[[vertex_id]]) {
      Varyings out;
      constant Vertex &v = verticies[vid];
      out.position = float4(float2(v.position), 0.0, 1.0);
      out.texcoord = v.texcoord;

      return out;
    }

    fragment half4 fragmentColorConversion(
        Varyings in[[stage_in]],
        texture2d<float, access::sample> textureY[[texture(0)]],
        texture2d<float, access::sample> textureU[[texture(1)]],
        texture2d<float, access::sample> textureV[[texture(2)]]) {
      constexpr sampler s(address::clamp_to_edge, filter::linear);
      float y;
      float u;
      float v;
      float r;
      float g;
      float b;
      // Conversion for YUV to rgb from http://www.fourcc.org/fccyvrgb.php
      y = textureY.sample(s, in.texcoord).r;
      u = textureU.sample(s, in.texcoord).r;
      v = textureV.sample(s, in.texcoord).r;
      u = u - 0.5;
      v = v - 0.5;
      r = y + 1.403 * v;
      g = y - 0.344 * u - 0.714 * v;
      b = y + 1.770 * u;

      float4 out = float4(r, g, b, 1.0);

      return half4(out);
    }
);


/*

						
"#include <metal_stdlib>\n"
"#pragma clang diagnostic ignored \"-Wparentheses-equality\"\n"
"using namespace metal;\n"
"\n"
"struct VertexShaderInput {\n"
"    float4 aPosition [[attribute(0)]];\n"
"    float2 aTextureCoord [[attribute(1)]];\n"
"};\n"
"struct VertexShaderOutput {\n"
"    float4 gl_Position [[position]];\n"
"    float2 vTextureCoord;\n"
"};\n"
"struct ShaderUniform {\n"
"};\n"
"    vertex VertexShaderOutput VertexShader(VertexShaderInput _mtl_i [[stage_in]], constant ShaderUniform& _mtl_u [[buffer(0)]])\n"	
"{\n"
"    VertexShaderOutput _mtl_o;\n"
	"    _mtl_o.gl_Position = _mtl_i.aPosition;\n"
"    _mtl_o.vTextureCoord = _mtl_i.aTextureCoord;\n"
"    _mtl_o.gl_Position.z = (_mtl_o.gl_Position.z + _mtl_o.gl_Position.w) / 2.0f;\n"
"    return _mtl_o;\n"
"}\n"
"struct FragmentShaderInput {\n"
"  float2 vTextureCoord;\n"
"};\n"
"struct FragmentShaderOutput {\n"
"  float4 fragColor;\n"
"};\n"
"fragment FragmentShaderOutput FragmentShader (FragmentShaderInput _mtl_i [[stage_in]], constant ShaderUniform& _mtl_u [[buffer(0)]]\n"
"  ,   texture2d<float> Ytex [[texture(0)]], sampler _mtlsmp_Ytex [[sampler(0)]]\n"
"  ,   texture2d<float> Utex [[texture(1)]], sampler _mtlsmp_Utex [[sampler(1)]]\n"
"  ,   texture2d<float> Vtex [[texture(2)]], sampler _mtlsmp_Vtex [[sampler(2)]])\n"
"{\n"
"  FragmentShaderOutput _mtl_o;\n"
"  float v_1 = 0;\n"
"  float u_2 = 0;\n"
"  float ny_3 = 0;\n"
"  float nx_4 = 0;\n"
"  nx_4 = _mtl_i.vTextureCoord.x;\n"
"  ny_3 = _mtl_i.vTextureCoord.y;\n"
"  float2 tmpvar_5 = 0;\n"
"  tmpvar_5.x = nx_4;\n"
"  tmpvar_5.y = ny_3;\n"
"  float4 tmpvar_6 = 0;\n"
"  tmpvar_6 = Ytex.sample(_mtlsmp_Ytex, float2((tmpvar_5).x, (1.0 - (tmpvar_5).y)));\n"
"  float2 tmpvar_7 = 0;\n"
"  tmpvar_7.x = nx_4;\n"
"  tmpvar_7.y = ny_3;\n"
"  float2 tmpvar_8 = 0;\n"
"  tmpvar_8.x = nx_4;\n"
"  tmpvar_8.y = ny_3;\n"
"  u_2 = (Utex.sample(_mtlsmp_Utex, float2((tmpvar_7).x, (1.0 - (tmpvar_7).y))).x - (float)(0.5));\n"
"  v_1 = (Vtex.sample(_mtlsmp_Vtex, float2((tmpvar_8).x, (1.0 - (tmpvar_8).y))).x - (float)(0.5));\n"
"  float4 tmpvar_9 = 0;\n"
"  tmpvar_9.w = 1.0;\n"
"  tmpvar_9.x = (tmpvar_6.x + ((float)(1.403) * v_1));\n"
"  tmpvar_9.y = ((tmpvar_6.x - ((float)(0.344) * u_2)) - ((float)(0.714) * v_1));\n"
"  tmpvar_9.z = (tmpvar_6.x + ((float)(1.77) * u_2));\n"
"  _mtl_o.fragColor = tmpvar_9;\n"
"  return _mtl_o;\n"
"}\n";


*/
