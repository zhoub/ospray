// Copyright 2009 Intel Corporation
// SPDX-License-Identifier: Apache-2.0

#include "rkcommon/math/LinearSpace.ih"
#include "rkcommon/math/vec.ih"
// c++ shared
#include "fb/pixel_ops/ToneMapperShared.h"

OSPRAY_BEGIN_ISPC_NAMESPACE

// ACES input transform matrix = RRT_SAT_MAT * XYZ_2_AP1_MAT * D65_2_D60_CAT *
// REC709_2_XYZ_PRI_MAT
static const uniform LinearSpace3f acesInputMat = {
    {0.5972782409f, 0.0760130499f, 0.0284085382f},
    {0.3545713181f, 0.9083220973f, 0.1338243154f},
    {0.0482176639f, 0.0156579968f, 0.8375684636f}};

// ACES output transform matrix = XYZ_2_REC709_PRI_MAT * D60_2_D65_CAT *
// AP1_2_XYZ_MAT * ODT_SAT_MAT
static const uniform LinearSpace3f acesOutputMat = {
    {1.6047539945f, -0.1020831870f, -0.0032670420f},
    {-0.5310794927f, 1.1081322801f, -0.0727552477f},
    {-0.0736720338f, -0.0060518756f, 1.0760219533f}};

inline vec3f toneMap(const LiveToneMapper *uniform self, const vec3f &col)
{
  vec3f x = col * self->exposure;
  if (self->acesColor)
    x = acesInputMat * x;
  x = pow(x, self->a) / (pow(x, self->a * self->d) * self->b + self->c);
  if (self->acesColor)
    x = acesOutputMat * x;
  x = clamp(x, make_vec3f(0.f), make_vec3f(1.f));
  return x;
}

vec4f LiveToneMapper_processPixel(void *uniform _self,
    const vec4f &color,
    const float,
    const vec3f &,
    const vec3f &)
{
  const LiveToneMapper *uniform self = (const LiveToneMapper *uniform)_self;

  vec3f tonemapped = toneMap(self, make_vec3f(color));
  return make_vec4f(tonemapped, color.w);
}

export void *uniform LiveToneMapper_processPixel_addr()
{
  return (void *uniform)LiveToneMapper_processPixel;
}
OSPRAY_END_ISPC_NAMESPACE
