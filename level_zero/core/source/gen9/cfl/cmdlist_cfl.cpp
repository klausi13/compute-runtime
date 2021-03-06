/*
 * Copyright (C) 2019-2020 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 *
 */

#include "shared/source/gen9/hw_cmds.h"
#include "shared/source/gen9/hw_info.h"

#include "level_zero/core/source/cmdlist_hw.inl"
#include "level_zero/core/source/cmdlist_hw_base.inl"
#include "level_zero/core/source/cmdlist_hw_immediate.inl"
#include "level_zero/core/source/gen9/cmdlist_gen9.h"
#include "level_zero/core/source/gen9/cmdlist_gen9.inl"

#include "cmdlist_extended.inl"
#include "igfxfmid.h"

namespace L0 {

static CommandListPopulateFactory<IGFX_COFFEELAKE, CommandListProductFamily<IGFX_COFFEELAKE>>
    populateCFL;

static CommandListImmediatePopulateFactory<IGFX_COFFEELAKE, CommandListImmediateProductFamily<IGFX_COFFEELAKE>>
    populateCFLImmediate;
} // namespace L0
