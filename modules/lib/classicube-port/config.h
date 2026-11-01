/* Forced into every ClassiCube translation unit (clang -include) ahead of Core.h.
   CC_BUILD_MANUAL skips Core.h's platform detection, so everything a platform block
   would set is chosen here. */
#pragma once

#define CC_BUILD_MANUAL
#define CC_BUILD_STRAWOS

/* no kernel threads: map generation runs in steps on the game thread, and HTTP requests
   run synchronously when queued (see the Http_Worker.c rewrite in classicube.sh) */
#define CC_BUILD_COOPTHREADED

/* no audio stack yet */
#define CC_BUILD_NOMUSIC
#define CC_BUILD_NOSOUNDS

#define CC_GFX_BACKEND CC_GFX_BACKEND_SOFTGPU
#define CC_AUD_BACKEND CC_AUD_BACKEND_NULL
#define CC_NET_BACKEND CC_NET_BACKEND_BUILTIN
#define CC_SSL_BACKEND CC_SSL_BACKEND_BEARSSL
