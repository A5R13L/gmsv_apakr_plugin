#pragma once

#ifdef BOOTIL_COMPILE_DLL
#ifdef _WIN32
#if defined(__GNUC__)
#define BOOTIL_EXPORT __attribute__((dllexport))
#else
#define BOOTIL_EXPORT __declspec(dllexport)
#endif
#else
#define BOOTIL_EXPORT
#endif
#else

#define BOOTIL_EXPORT
#endif

// Standards
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <limits.h>

// STL
#include <string>
#include <vector>
#include <list>
#include <map>
#include <stack>
#include <sstream>
#include <queue>
#include <set>
#include <algorithm>
#include <fstream>
#include <iostream>

//
// Forward declarations
//

namespace Bootil
{
typedef std::string BString;
typedef std::wstring WString;
} // namespace Bootil

#include "Bootil/Types/Buffer.h"

namespace Bootil
{
template <typename T> void SafeDelete(T *&Ptr)
{
    delete Ptr;
    Ptr = NULL;
}

template <typename T> void SafeRelease(T *&Ptr)
{
    if (!Ptr)
    {
        return;
    }

    Ptr->Release();
    Ptr = NULL;
}

template <typename T> T Min(T a, T b)
{
    return a < b ? a : b;
}

template <typename T> T Max(T a, T b)
{
    return a > b ? a : b;
}

template <typename T> T Clamp(T val, T min, T max)
{
    if (val < min)
        return min;
    if (val > max)
        return max;
    return val;
}

} // namespace Bootil
