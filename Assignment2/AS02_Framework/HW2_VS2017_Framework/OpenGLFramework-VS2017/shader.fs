#version 330 core

out vec4 FragColor;
in vec3 vertex_color;
in vec3 vertex_normal;

void main() {
	// [TODO]
	FragColor = vec4(vertex_normal, 1.0f);
}
