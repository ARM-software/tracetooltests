#version 450
layout(xfb_buffer = 0, xfb_stride = 16) out;
layout(location = 0, xfb_offset = 0) out vec4 captured_position;

vec2 positions[3] = vec2[](
	vec2(-0.5, -0.5),
	vec2(0.5, -0.5),
	vec2(0.0, 0.5)
);

void main()
{
	vec4 position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
	captured_position = position;
	gl_Position = position;
}
