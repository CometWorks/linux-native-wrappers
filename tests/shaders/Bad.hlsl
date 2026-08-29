// Fails to compile on purpose: the diagnostics test checks the line, column
// and error code the compiler reports through the CRT printf shims.
float4 PS() : SV_Target
{
    float4 color = float4(1, 0, 0, 1);
    color.xyz = undeclared_thing * 2;
    return color;
}
