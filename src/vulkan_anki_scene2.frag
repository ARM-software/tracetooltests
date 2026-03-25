#version 460
#extension GL_EXT_ray_query : require
#extension GL_EXT_buffer_reference : require

layout(location = 0) in vec2 outUv;
layout(location = 0) out vec4 fragColor;

layout(set = 0, binding = 0) uniform CameraParams
{
	vec4 originScale;
} cameraParams;

layout(set = 0, binding = 1) uniform accelerationStructureEXT topLevelAS;
layout(set = 0, binding = 2) uniform sampler2D albedoTex;

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer MaterialBlock
{
	vec4 tint;
	vec4 params;
};

layout(buffer_reference, std430, buffer_reference_align = 16) readonly buffer HighlightBlock
{
	vec4 accent;
};

layout(set = 0, binding = 3) uniform ScenePointers
{
	MaterialBlock material;
} scenePointers;

layout(set = 0, binding = 4) uniform FrameConfig
{
	vec4 params;
} frameConfig;

layout(std430, push_constant) uniform PushConstants
{
	HighlightBlock highlight;
	float blendFactor;
	vec3 _padding;
} pushConstants;

void main()
{
	vec2 ndc = outUv * 2.0 - 1.0;
	vec3 origin = vec3(ndc * cameraParams.originScale.w, cameraParams.originScale.z);
	vec3 direction = normalize(vec3(0.0, 0.0, -1.0));

	rayQueryEXT rayQuery;
	rayQueryInitializeEXT(rayQuery, topLevelAS, gl_RayFlagsOpaqueEXT, 0xFFu, origin, 0.0, direction, 8.0);
	while(rayQueryProceedEXT(rayQuery))
	{
	}

	vec3 texel = texture(albedoTex, outUv * frameConfig.params.zw).rgb;
	vec3 baseColor = texel * scenePointers.material.tint.rgb;
	vec3 accentColor = pushConstants.highlight.accent.rgb;
	vec3 shaded = mix(baseColor, accentColor, pushConstants.blendFactor);

	if(rayQueryGetIntersectionTypeEXT(rayQuery, true) != gl_RayQueryCommittedIntersectionNoneEXT)
	{
		float hitT = rayQueryGetIntersectionTEXT(rayQuery, true);
		float distanceFade = clamp(1.0 - (hitT * 0.18), 0.2, 1.0);
		float roughness = clamp(scenePointers.material.params.x, 0.0, 1.0);
		vec3 reflected = mix(shaded, accentColor, roughness * 0.35);
		fragColor = vec4(reflected * distanceFade * frameConfig.params.x, 1.0);
	}
	else
	{
		vec3 sky = mix(vec3(0.04, 0.06, 0.10), texel * 0.3, outUv.y);
		fragColor = vec4(sky, 1.0);
	}
}
