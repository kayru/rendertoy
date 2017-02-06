uniform restrict writeonly image2D outputTex;	//@ relativeTo(inputImage)
layout(rgba16f) uniform restrict readonly image2D inputImage1;	//@ input
layout(rgba16f) uniform restrict readonly image2D inputImage2;	//@ input

layout (local_size_x = 8, local_size_y = 8) in;
void main() {
	ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
	vec4 col = imageLoad(inputImage1, pix);
	col += imageLoad(inputImage2, pix);
	imageStore(outputTex, pix, col);
}
