call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" arm64
cl test_vk_pipeline.c /I C:\Users\nando\Documents\antigravity\sharp-babbage\dm\third_party\vulkan-headers\include
test_vk_pipeline.exe
