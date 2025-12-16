#version 450

layout(location = 0) in vec3 inPosition;

struct ObjectData
{
    mat4 model;
    vec4 baseColor;
};

layout(set = 0, binding = 0) uniform ObjectDataBuffer
{
    ObjectData data[1024];
} object;

layout(std430, push_constant) uniform CameraData
{
    mat4 view;
    mat4 proj;
} camera;

layout(location = 0) out vec4 vColor;

void main()
{
    uint idx = gl_InstanceIndex;

    mat4 vp = camera.proj * camera.view;
    ObjectData data = object.data[idx];
    gl_Position = vp * (data.model * vec4(inPosition, 1.0));

    vColor = data.baseColor;
}
