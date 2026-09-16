#version 450

layout(set = 0, binding = 0, std430) readonly buffer TriangleColors
{
	vec4 color[3];
} colors;

layout(location = 0) in vec2 in_position;
layout(location = 0) out vec4 out_color;

void main()
{
	gl_Position = vec4(in_position, 0.0, 1.0);
	out_color = colors.color[gl_VertexIndex];
}
