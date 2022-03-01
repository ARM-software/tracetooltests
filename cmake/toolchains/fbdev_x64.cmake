SET(CMAKE_SYSTEM_NAME Linux)
SET(ARCH x64)
set(CC_CFLAGS "-m64")
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")
