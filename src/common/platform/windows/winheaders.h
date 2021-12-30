#ifndef TWILIGHT_COMMON_PLATFORM_WINDOWS_WINHEADERS_H
#define TWILIGHT_COMMON_PLATFORM_WINDOWS_WINHEADERS_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <d3d11.h>
#include <d3d9types.h>
#include <d3dcompiler.h>

#include <dxgi1_5.h>

#include <codecapi.h>
#include <icodecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mftransform.h>
#include <propvarutil.h>

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <timeapi.h>

#endif