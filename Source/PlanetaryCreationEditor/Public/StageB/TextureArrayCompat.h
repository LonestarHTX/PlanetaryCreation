#pragma once

// TextureArrayCompat.h
// Compatibility shim for Engine/Texture2DArray.h to unblock editor/test builds when GPU path is disabled.

#if !PLANETARYCREATION_DISABLE_STAGEB_GPU
#include "Engine/Texture2DArray.h"
#else
// When GPU is disabled, avoid including the engine header.
// Forward declare the type so pointers can be declared; do not provide methods.
class UTexture2DArray;
#endif
