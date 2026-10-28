#define VK_USE_PLATFORM_WIN32_KHR  
#include <windows.h>  
#include <stdio.h>  
#include "dm/third_party/vulkan-headers/include/vulkan/vulkan.h"  
int main() { HMODULE h = LoadLibraryA("vulkan-1.dll"); if(!h) { printf("no vk dll\n"); return 1; } PFN_vkGetInstanceProcAddr get_proc = (PFN_vkGetInstanceProcAddr)GetProcAddress(h, "vkGetInstanceProcAddr"); PFN_vkCreateInstance create_inst = (PFN_vkCreateInstance)get_proc(NULL, "vkCreateInstance"); VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "test", 1, "test", 1, VK_API_VERSION_1_3 }; const char *exts[] = { "VK_KHR_surface", "VK_KHR_win32_surface" }; VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &ai, 0, NULL, 2, exts }; VkInstance inst; if (create_inst(&ci, NULL, &inst) != VK_SUCCESS) { printf("create inst failed\n"); return 2; } printf("vk instance created successfully!\n"); return 0; }  
