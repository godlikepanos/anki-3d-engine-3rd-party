#version 450 core
out gl_PerVertex
{
	vec4 gl_Position;
};

void main()
{
	const vec2 POSITIONS[3] =
		vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));

	gl_Position = vec4(POSITIONS[gl_VertexID], 0.0, 1.0);
}
