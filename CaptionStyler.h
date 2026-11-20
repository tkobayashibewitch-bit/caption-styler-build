#pragma once
#ifndef CAPTIONSTYLER_H
#define CAPTIONSTYLER_H

#define PF_DEEP_COLOR_AWARE 1   // get 16bpc pixel types from AE_Effect.h

#include "AEConfig.h"
#ifdef AE_OS_WIN
    #include <Windows.h>
#endif
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_EffectCBSuites.h"
#include "AEGP_SuiteHandler.h"
#include "AEFX_SuiteHelper.h"

#define PLUGIN_NAME        "Caption Styler"
#define PLUGIN_MATCH_NAME  "TKB CaptionStyler"
#define PLUGIN_CATEGORY    "Caption Styler"
#define PLUGIN_DESCRIPTION "Multi-stroke captions, drop shadow and background box on a single text layer."

#define MAJOR_VERSION   1
#define MINOR_VERSION   0
#define BUG_VERSION     0
#define STAGE_VERSION   PF_Stage_RELEASE
#define BUILD_VERSION   1

#define NUM_STROKES 3

// Each group = TOPIC, items..., TOPIC_END
// Stroke group: TOPIC, ON, COLOR, WIDTH, END  (5 params)
enum {
    CS_INPUT = 0,

    CS_STROKE_BASE = 1,                 // stroke i uses CS_STROKE_BASE + i*5 + k
    CS_STROKE_STRIDE = 5,

    CS_SH_TOPIC = CS_STROKE_BASE + NUM_STROKES * CS_STROKE_STRIDE,  // 16
    CS_SH_ON,
    CS_SH_COLOR,
    CS_SH_OPACITY,
    CS_SH_DISTANCE,
    CS_SH_ANGLE,
    CS_SH_SOFTNESS,
    CS_SH_END,

    CS_BX_TOPIC,
    CS_BX_ON,
    CS_BX_COLOR,
    CS_BX_OPACITY,
    CS_BX_PAD_X,
    CS_BX_PAD_Y,
    CS_BX_ROUND,
    CS_BX_END,

    CS_NUM_PARAMS
};

enum { STK_TOPIC = 0, STK_ON, STK_COLOR, STK_WIDTH, STK_END };

inline int StrokeParam(int i, int k) { return CS_STROKE_BASE + i * CS_STROKE_STRIDE + k; }

// Disk IDs: keep stable forever once shipped (saved projects depend on them)
inline int StrokeDiskID(int i, int k) { return 100 + i * 10 + k; }
enum {
    DID_SH_TOPIC = 200, DID_SH_ON, DID_SH_COLOR, DID_SH_OPACITY, DID_SH_DISTANCE,
    DID_SH_ANGLE, DID_SH_SOFTNESS, DID_SH_END,
    DID_BX_TOPIC = 300, DID_BX_ON, DID_BX_COLOR, DID_BX_OPACITY, DID_BX_PAD_X,
    DID_BX_PAD_Y, DID_BX_ROUND, DID_BX_END
};

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                            PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}

#endif
