/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/gen9/hw_cmds.h"
#include "shared/source/gen9/hw_info.h"

#include "level_zero/core/source/image_hw.inl"

namespace L0 {

template <>
struct ImageProductFamily<IGFX_GEMINILAKE> : public ImageCoreFamily<IGFX_GEN9_CORE> {
    using ImageCoreFamily::ImageCoreFamily;
};

static ImagePopulateFactory<IGFX_GEMINILAKE, ImageProductFamily<IGFX_GEMINILAKE>> populateGLK;

} // namespace L0
