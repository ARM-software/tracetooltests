#version 450

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec2 inUv;

layout(location = 0) out vec2 outUv;

void main()
{
	outUv = inUv;
	gl_Position = vec4(inPos, 0.0, 1.0);
}
