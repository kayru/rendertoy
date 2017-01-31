uniform restrict image2D outputTex;
uniform int blurRadius;	//@ max(30)
uniform ivec2 blurDir;	//@ min(0) max(1)
layout(rgba16f) uniform restrict readonly image2D someImage;	//@ input default

layout (local_size_x = 8, local_size_y = 8) in;
void main() {
	ivec2 pix = ivec2(gl_GlobalInvocationID.xy);
	vec4 col = imageLoad(someImage, pix);
	for (int i = 1; i <= blurRadius; ++i) {
		col += imageLoad(someImage, pix + blurDir * i);
		col += imageLoad(someImage, pix - blurDir * i);
	}
	col *= 1.0 / (1 + 2 * blurRadius);
	imageStore(outputTex, pix, col);
}
