#ifndef ZTRACING_SRC_CONFIG_H_
#define ZTRACING_SRC_CONFIG_H_

// Clang OS/Arch Detection
// ----------------------------------------------------------------------------

#if defined(__clang__)

#define COMPILER_CLANG 1

#if defined(_WIN32)
#define OS_WINDOWS 1
#elif __EMSCRIPTEN__
#define OS_EMSCRIPTEN 1
#elif defined(__gnu_linux__) || defined(__linux__)
#define OS_LINUX 1
#elif defined(__APPLE__) && defined(__MACH__)
#define OS_MAC 1
#else
#error This Compiler/OS combo is not supported.
#endif

// MSVC OS/Arch Detection
// ----------------------------------------------------------------------------

#elif defined(_MSC_VER)

#define COMPILER_MSVC 1

#if defined(_WIN32)
#define OS_WINDOWS 1
#else
#error This Compiler/OS combo is not supported.
#endif

#else
#error Compiler not supported.
#endif

#if !defined(NDEBUG)
#define ZTRACING_DEBUG 1
#endif

#endif  // ZTRACING_SRC_CONFIG_H_
