#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include <AE_General.r>
#endif

resource 'PiPL' (16000) {
	{
		Kind { AEEffect },
		Name { "Caption Styler" },
		Category { "Caption Styler" },
#ifdef AE_OS_WIN
	#if defined(AE_PROC_INTELx64)
		CodeWin64X86 { "EffectMain" },
	#elif defined(AE_PROC_ARM64)
		CodeWinARM64 { "EffectMain" },
	#endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 { "EffectMain" },
		CodeMacARM64 { "EffectMain" },
#endif
		AE_PiPL_Version { 2, 0 },
		AE_Effect_Spec_Version { PF_PLUG_IN_VERSION, PF_PLUG_IN_SUBVERS },
		AE_Effect_Version { 525825 },             /* PF_VERSION(1,0,0,RELEASE,1) */
		AE_Effect_Info_Flags { 0 },
		AE_Effect_Global_OutFlags { 0x02000200 },  /* DEEP_COLOR_AWARE | I_EXPAND_BUFFER */
		AE_Effect_Global_OutFlags_2 { 0x08000000 },/* SUPPORTS_THREADED_RENDERING */
		AE_Effect_Match_Name { "TKB CaptionStyler" },
		AE_Reserved_Info { 0 },
		AE_Effect_Support_URL { "" }
	}
};
