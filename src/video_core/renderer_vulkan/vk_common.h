// Copyright 2023 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

// Every translation unit that reaches <vulkan/vulkan.hpp> through this header must see the same
// platform surface macro, or the generated vk::DispatchLoaderDynamic layout differs across TUs
// (an ODR violation LTO will flag). This has to live here, not in vk_platform.cpp alone, because
// vk_common.cpp (which defines VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE) and the other
// headers below include this file directly, without going through vk_platform.cpp's own copy of
// this ifdef ladder.
#if defined(ANDROID)
#define VK_USE_PLATFORM_ANDROID_KHR
#elif defined(WIN32)
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__APPLE__)
#define VK_USE_PLATFORM_METAL_EXT
#elif defined(__SWITCH__)
#define VK_USE_PLATFORM_VI_NN
#else
#define VK_USE_PLATFORM_WAYLAND_KHR
#define VK_USE_PLATFORM_XLIB_KHR
#endif

// Include vulkan-hpp header
#define VK_ENABLE_BETA_EXTENSIONS
#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#define VULKAN_HPP_NO_CONSTRUCTORS
#define VULKAN_HPP_NO_STRUCT_SETTERS
#ifdef __SWITCH__
// Horizon has no dlopen nor does it need it whatsoever.
#define VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL 0
#endif
#include <vulkan/vulkan.hpp>

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
