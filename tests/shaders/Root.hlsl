#include "Sub/Local.hlsli"
#include <SystemInc.hlsli>

float4 __pixel_shader() : SV_Target
{
    return LOCAL_VALUE + SYSTEM_VALUE + NESTED_VALUE + EXTRA_VALUE;
}
