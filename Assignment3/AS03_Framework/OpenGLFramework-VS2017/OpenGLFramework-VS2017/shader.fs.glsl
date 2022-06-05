#version 330

in vec2 texCoord;

out vec4 fragColor;

// [TODO] passing texture from main.cpp
// Hint: sampler2D

void main() {
	fragColor = vec4(texCoord.xy, 0, 1);

	// [TODO] sampleing from texture
	// Hint: texture
}
