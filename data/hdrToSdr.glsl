uniform restrict image2D outputTex;	//@ relativeTo(inputImage)
uniform float EV;	//@ min(-8) max(8)
uniform vec3 tint;	//@ color 
layout(rgba16f) uniform restrict readonly image2D inputImage;	//@ input

layout (local_size_x = 8, local_size_y = 8) in;
void main() {
	ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
	vec4 col = imageLoad(inputImage, pix);
	col *= exp(EV);
	col.rgb *= tint;
	col = 1.0 - exp(-col);
	imageStore(outputTex, pix, col);
}
