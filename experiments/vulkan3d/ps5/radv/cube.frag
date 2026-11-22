#version 450
layout(location = 0) in vec3 faceColor;
layout(location = 0) out vec4 outputColor;

void main()
{
    outputColor = vec4(faceColor, 1.0);
}
