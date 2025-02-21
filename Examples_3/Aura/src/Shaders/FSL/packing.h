/*
 * Copyright (c) 2018-2021 Confetti Interactive Inc.
 *
 * This is a part of Aura.
 *
 * This file(code) is licensed under a
 * Creative Commons Attribution-NonCommercial 4.0 International License
 *
 *   (https://creativecommons.org/licenses/by-nc/4.0/legalcode)
 *
 * Based on a work at https://github.com/ConfettiFX/The-Forge.
 * You may not use the material for commercial purposes.
*/

#ifndef PACKING_H
#define PACKING_H

float2 OctWrap(float2 v)
{
	return (1.0 - abs(v.yx)) * float2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

float3 decodeDir(float2 encN)
{
	encN = encN * 2.0 - 1.0;

	float3 n;
	n.z = 1.0 - abs(encN.x) - abs(encN.y);
	n.xy = n.z >= 0.0 ? encN.xy : OctWrap(encN.xy);
	n = normalize(n);
	return n;
}

float2 encodeDir(float3 n)
{
	n /= (abs(n.x) + abs(n.y) + abs(n.z));
	n.xy = n.z >= 0.0 ? n.xy : OctWrap(n.xy);
	n.xy = n.xy * 0.5 + 0.5;
	return n.xy;
}

float2 sign_not_zero(float2 v)
{
	return step(0.0, v) * 2.0 - float2(1, 1);
}

#if !defined(VULKAN) && !defined(METAL)
uint packSnorm2x16(float2 v)
{
	uint2 SNorm = uint2(round(clamp(v, -1, 1) * 32767.0));
	return (0x0000FFFF & SNorm.x) | ((SNorm.y << 16) & 0xFFFF0000);
}

float2 unpackSnorm2x16(uint p)
{
	half2 ret = half2(
		clamp((0x0000FFFF & p) / 32767.0, -1, 1),
		clamp(((0xFFFF0000 & p) >> 16) / 32767.0, -1, 1)
	);

	return ret;
}

uint packUnorm2x16(float2 v)
{
	uint2 UNorm = uint2(round(saturate(v) * 65535.0));
	return (0x0000FFFF & UNorm.x) | ((UNorm.y << 16) & 0xFFFF0000);
}

float2 unpackUnorm2x16(uint p)
{
	float2 ret;
	ret.x = saturate((0x0000FFFF & p) / 65535.0);
	ret.y = saturate(((0xFFFF0000 & p) >> 16) / 65535.0);
	return ret;
}


uint packUnorm4x8(float4 v)
{
	uint4 UNorm = uint4(round(saturate(v) * 255.0));
	return (0x000000FF & UNorm.x) | ((UNorm.y << 8) & 0x0000FF00) | ((UNorm.z << 16) & 0x00FF0000) | ((UNorm.w << 24) & 0xFF000000);
}

float4 unpackUnorm4x8(uint p)
{
	return float4(float(p & 0x000000FF) / 255.0,
		float((p & 0x0000FF00) >> 8) / 255.0,
		float((p & 0x00FF0000) >> 16) / 255.0,
		float((p & 0xFF000000) >> 24) / 255.0);
}
#endif

#ifndef METAL
uint pack2Floats(float low, float high)
{
#ifdef VULKAN
	return packHalf2x16(vec2(low, high));
#else
	return uint((f32tof16(low) & 0xFFFF) | ((f32tof16(high) & 0xFFFF) << 16));
#endif
}

float2 unpack2Floats(uint p)
{
#ifdef VULKAN
	return unpackHalf2x16(p);
#else
	return float2(f16tof32(p & 0xFFFF), f16tof32((p >> 16) & 0xFFFF));
#endif
}

uint pack2Snorms(float low, float high)
{
#ifdef VULKAN
	return packSnorm2x16(vec2(low, high));
#else
	return packSnorm2x16(half2(low, high));
#endif
}

float2 unpack2Snorms(uint p)
{
	return float2(unpackSnorm2x16(p));
}

uint pack2Unorms(float low, float high)
{
#ifdef VULKAN
	return packUnorm2x16(vec2(low, high));
#else
	return packUnorm2x16(half2(low, high));
#endif
}

float2 unpack2Unorms(uint p)
{
	return float2(unpackUnorm2x16(p));
}
#else
float2 unpack2Floats(uint p)
{
	return float2(as_type<half2>(p));
}
#define unpackUnorm2x16 unpack_unorm2x16_to_float
#define packUnorm4x8    pack_float_to_unorm4x8
#define unpackUnorm4x8  unpack_unorm4x8_to_float
#endif // METAL

#endif // PACKING_H
