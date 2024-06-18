#include <stdio.h>
#include <string.h>

#include <windows.h>
#include <dxgi.h>
#include <d3dcompiler.h>

#include "susano.h"

#include "linebatchrenderer.h"
#include "defaultgeorenderer.h"

#include "minhook/minhook.h"

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

/////////////////////////////////////////////////
// Macros
/////////////////////////////////////////////////

#define PRINT_LAST_ERROR() printf("0x%08X\n", GetLastError())

#define ARRAY_LENGTH(ARRAY) (sizeof(ARRAY) / sizeof((ARRAY)[0]))

#define PAGE_SIZE (0x1000)

#define ALIGN_PAGE_DOWN(VALUE) (((UINT64)VALUE) & ~((PAGE_SIZE) - 1))
#define ALIGN_PAGE_UP(VALUE) ((((UINT64)VALUE) + ((PAGE_SIZE) - 1)) & ~((PAGE_SIZE) - 1))

#define DEREF_POINTER(POINTER) ((UINT64)(*(PVOID*)(POINTER)))

#define DEG_TO_RAD(DEGREES) (DEGREES * ((FLOAT)0.01745329251994329576923690768489))
#define RAD_TO_DEG(RADIANS) (RADIANS * ((FLOAT)57.295779513082320876798154814105))

#define HR_CHECK(EXPRESSION) \
    { \
        HRESULT result = (EXPRESSION); \
        if (result != S_OK) \
        { \
            printf("%s 0x%08X\n", #EXPRESSION, result); \
        } \
    }

#define COPY_INTO_CONSTANT_BUFFER(BUFFER, VALUE, SIZE) \
    { \
        D3D11_MAPPED_SUBRESOURCE mappedSubResource = { 0 }; \
        HR_CHECK(gDeviceContext->Map((BUFFER), NULL, D3D11_MAP_WRITE_DISCARD, NULL, &mappedSubResource)); \
        memcpy(mappedSubResource.pData, (VALUE), (SIZE)); \
        gDeviceContext->Unmap((BUFFER), NULL); \
    }

/////////////////////////////////////////////////
// Type Definition
/////////////////////////////////////////////////

typedef UINT32(*PRESENT_PROC)(IDXGISwapChain*, UINT32, UINT32);

/////////////////////////////////////////////////
// External Definition
/////////////////////////////////////////////////

extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND Window, UINT32 Msg, WPARAM WParam, LPARAM LParam);

/////////////////////////////////////////////////
// Global Variables
/////////////////////////////////////////////////

ID3D11Device* gDevice = NULL;
ID3D11DeviceContext* gDeviceContext = NULL;
ID3D11RenderTargetView* gMainRenderTargetView = NULL;
ID3D11Buffer* gModelViewProjectionBuffer = NULL;

MODEL_VIEW_PROJECTION gModelViewProjection;

/////////////////////////////////////////////////
// Local Variables
/////////////////////////////////////////////////

static HANDLE sMainThread = NULL;
static HANDLE sMainThreadExitEvent = NULL;

static FILE* sConsoleInputStream = NULL;
static FILE* sConsoleOutputStream = NULL;

static UINT64 sOkamiExeBase = 0;
static UINT64 sFlowerKernelDllBase = 0;
static UINT64 sMainDllBase = 0;

static BOOL sRunning = TRUE;
static BOOL sPresentInitialized = FALSE;
static BOOL sAllowModelViewProjectionUpdate = TRUE; // TODO

static PRESENT_PROC sPresentOld = NULL;
static PRESENT_PROC sPresentNew = NULL;

static HWND sWindow = NULL;
static WNDPROC sWindowProc = NULL;

static ImGuiContext* sImGuiContext = NULL;

// TODO
static FLOAT sScaleFactor = 1.0f;
static FLOAT sPitchFactor = 1.0f;
static FLOAT sYawFactor = 1.0f;
static FLOAT sYawOffset = 0.0f;
static FLOAT sOrbitalPitch = 0.0f;
static FLOAT sOrbitalYaw = 0.0f;

/////////////////////////////////////////////////
// Function Definition
/////////////////////////////////////////////////

VOID VaCreateConsole(VOID);
VOID VaDestroyConsole(VOID);

UINT64 VaFindModuleBase(LPCSTR ModuleName);

VOID VaReadFromMemory(HANDLE Process, UINT64 Base, UINT64 Size, PVOID Buffer);
VOID VaWriteIntoMemory(HANDLE Process, UINT64 Base, UINT64 Size, PVOID Buffer);

VOID VaCreateRelCall(PBYTE Bytes, UINT32 Address);
VOID VaCreateRelJmp(PBYTE Bytes, UINT32 Address);

VOID VaCreateAbsCall(PBYTE Bytes, UINT64 Address);
VOID VaCreateAbsJmp(PBYTE Bytes, UINT64 Address);

VOID VaGameplayFixes(VOID);

UINT64 VaAllocatePageNearAddress(UINT64 Base);

PVOID VaGetPresentPointer(VOID);

VOID VaCreateConstantBuffers(VOID);

VOID VaUpdateViewport(VOID);
VOID VaUpdateModelViewProjection(VOID);

VOID VaRenderDirectX(VOID);
VOID VaRenderImGui(VOID);

UINT32 VaDetourPresent(IDXGISwapChain* SwapChain, UINT32 SyncInterval, UINT32 Flags);

LRESULT VaWindowProc(HWND Window, UINT32 Msg, WPARAM WParam, LPARAM LParam);

VOID VaCleanupDirectX(VOID);
VOID VaCleanupWindow(VOID);
VOID VaCleanupImGui(VOID);

INT32 WINAPI VaMainThread(PVOID UserParam);

/////////////////////////////////////////////////
// Function Implementation
/////////////////////////////////////////////////

VOID VaCreateConsole(VOID)
{
    AllocConsole();

    freopen_s(&sConsoleInputStream, "CONIN$", "r", stdin);
    freopen_s(&sConsoleOutputStream, "CONOUT$", "w", stdout);
}
VOID VaDestroyConsole(VOID)
{
    fclose(sConsoleOutputStream);
    fclose(sConsoleInputStream);

    FreeConsole();
}

UINT64 VaFindModuleBase(LPCSTR ModuleName)
{
    HMODULE module = GetModuleHandleA(ModuleName);

    MEMORY_BASIC_INFORMATION info = { 0 };
    VirtualQuery(module, &info, sizeof(info));

    return (UINT64)info.AllocationBase;
}

VOID VaReadFromMemory(HANDLE Process, UINT64 Base, UINT64 Size, PVOID Buffer)
{
    UINT64 pageBase = ALIGN_PAGE_DOWN(Base);
    UINT64 pageSize = ALIGN_PAGE_UP(Size);

    DWORD oldProtect = 0;

    if (VirtualProtectEx(Process, (PVOID)pageBase, pageSize, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        ReadProcessMemory(Process, (PVOID)Base, Buffer, Size, NULL);
        VirtualProtectEx(Process, (PVOID)pageBase, pageSize, oldProtect, &oldProtect);
    }
}
VOID VaWriteIntoMemory(HANDLE Process, UINT64 Base, UINT64 Size, PVOID Buffer)
{
    UINT64 pageBase = ALIGN_PAGE_DOWN(Base);
    UINT64 pageSize = ALIGN_PAGE_UP(Size);

    DWORD oldProtect = 0;

    if (VirtualProtectEx(Process, (PVOID)pageBase, pageSize, PAGE_EXECUTE_READWRITE, &oldProtect))
    {
        WriteProcessMemory(Process, (PVOID)Base, Buffer, Size, NULL);
        VirtualProtectEx(Process, (PVOID)pageBase, pageSize, oldProtect, &oldProtect);
    }
}

VOID VaCreateRelCall(PBYTE Bytes, UINT32 Address)
{
    Bytes[0] = 0xE8;

    memcpy(Bytes + 1, &Address, sizeof(Address));
}
VOID VaCreateRelJmp(PBYTE Bytes, UINT32 Address)
{
    Bytes[0] = 0xE9;

    memcpy(Bytes + 1, &Address, sizeof(Address));
}

VOID VaCreateAbsCall(PBYTE Bytes, UINT64 Address)
{
    Bytes[0] = 0x48;
    Bytes[1] = 0xB8;

    memcpy(Bytes + 2, &Address, sizeof(Address));

    Bytes[10] = 0xFF;
    Bytes[11] = 0xD0;
}
VOID VaCreateAbsJmp(PBYTE Bytes, UINT64 Address)
{
    Bytes[0] = 0x48;
    Bytes[1] = 0xB8;

    memcpy(Bytes + 2, &Address, sizeof(Address));

    Bytes[10] = 0xFF;
    Bytes[11] = 0xE0;
}

VOID VaApplyGameplayFixes(VOID)
{
    *(PBYTE)(sMainDllBase + 0xB6AC45) = 1; // Framerate divisor (60 / X)
}

UINT64 VaAllocatePageNearAddress(UINT64 Base)
{
    SYSTEM_INFO sysInfo = { 0 };

    GetSystemInfo(&sysInfo);

    UINT64 pageAddr = ALIGN_PAGE_DOWN(Base);

    UINT64 minAddr = min(pageAddr - 0x7FFFFF00, (UINT64)sysInfo.lpMinimumApplicationAddress);
    UINT64 maxAddr = max(pageAddr + 0x7FFFFF00, (UINT64)sysInfo.lpMaximumApplicationAddress);

    UINT64 startPage = (pageAddr - (pageAddr % PAGE_SIZE));

    UINT64 pageOffset = 1;

    while (TRUE)
    {
        UINT64 byteOffset = pageOffset * PAGE_SIZE;
        UINT64 highAddr = startPage + byteOffset;
        UINT64 lowAddr = (startPage > byteOffset) ? startPage - byteOffset : 0;

        BOOL needsExit = (highAddr > maxAddr) && (lowAddr < minAddr);

        if (highAddr < maxAddr)
        {
            PVOID outAddr = VirtualAlloc((PVOID)highAddr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

            if (outAddr)
            {
                return (UINT64)outAddr;
            }
        }

        if (lowAddr > minAddr)
        {
            PVOID outAddr = VirtualAlloc((PVOID)lowAddr, PAGE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);

            if (outAddr)
            {
                return (UINT64)outAddr;
            }
        }

        pageOffset++;

        if (needsExit)
        {
            break;
        }
    }

    return 0;
}

PVOID VaGetPresentPointer(VOID)
{
    DXGI_SWAP_CHAIN_DESC swapChainDescription = { 0 };
    swapChainDescription.BufferCount = 2;
    swapChainDescription.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDescription.OutputWindow = FindWindowExW(0, 0, 0, L"ŌKAMI HD");
    swapChainDescription.SampleDesc.Count = 1;
    swapChainDescription.Windowed = TRUE;
    swapChainDescription.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    IDXGISwapChain* swapChain = NULL;
    ID3D11Device* device = NULL;

    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    
    HR_CHECK(D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, featureLevels, ARRAY_LENGTH(featureLevels), D3D11_SDK_VERSION, &swapChainDescription, &swapChain, &device, NULL, NULL));

    PVOID* vtable = *(PVOID**)swapChain;

    device->Release();
    swapChain->Release();

    return vtable[8];
}

VOID VaCreateConstantBuffers(VOID)
{
    D3D11_BUFFER_DESC bufferDescription = { 0 };
    bufferDescription.Usage = D3D11_USAGE_DYNAMIC;
    bufferDescription.ByteWidth = sizeof(MODEL_VIEW_PROJECTION);
    bufferDescription.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HR_CHECK(gDevice->CreateBuffer(&bufferDescription, NULL, &gModelViewProjectionBuffer));
}

VOID VaUpdateViewport(VOID)
{
    UINT32 windowWidth = *(PUINT32)(sFlowerKernelDllBase + 0x11C2C0);
    UINT32 windowHeight = *(PUINT32)(sFlowerKernelDllBase + 0x11C2C4);

    D3D11_VIEWPORT viewport = { 0 };
    viewport.TopLeftX = 0;
    viewport.TopLeftY = 0;
    viewport.Width = (FLOAT)windowWidth;
    viewport.Height = (FLOAT)windowHeight;

    gDeviceContext->RSSetViewports(1, &viewport);
}
VOID VaUpdateModelViewProjection(VOID)
{
    if (sAllowModelViewProjectionUpdate)
    {
        UINT32 windowWidth = *(PUINT32)(sFlowerKernelDllBase + 0x11C2C0);
        UINT32 windowHeight = *(PUINT32)(sFlowerKernelDllBase + 0x11C2C4);

        FLOAT aspectRatio = ((FLOAT)windowWidth) / windowHeight;

        UINT64 ammyObjBase = DEREF_POINTER(sMainDllBase + 0xB6B2D0);

        FLOAT playerX = *(PFLOAT)(ammyObjBase + 0x80);
        FLOAT playerY = *(PFLOAT)(ammyObjBase + 0x84);
        FLOAT playerZ = *(PFLOAT)(ammyObjBase + 0x88);

        // Center
        //FLOAT cameraX = *(PFLOAT)(sMainDllBase + 0xB66370);
        //FLOAT cameraY = *(PFLOAT)(sMainDllBase + 0xB66374);
        //FLOAT cameraZ = *(PFLOAT)(sMainDllBase + 0xB66378);

        //FLOAT cameraX = *(PFLOAT)(sMainDllBase + 0xB66380);
        //FLOAT cameraY = *(PFLOAT)(sMainDllBase + 0xB66384);
        //FLOAT cameraZ = *(PFLOAT)(sMainDllBase + 0xB66388);

        //FLOAT cameraX = *(PFLOAT)(sMainDllBase + 0xB665E0);
        //FLOAT cameraY = *(PFLOAT)(sMainDllBase + 0xB665E4);
        //FLOAT cameraZ = *(PFLOAT)(sMainDllBase + 0xB665E8);

        //FLOAT cameraX = *(PFLOAT)(sMainDllBase + 0xB665D0);
        //FLOAT cameraY = *(PFLOAT)(sMainDllBase + 0xB665D4);
        //FLOAT cameraZ = *(PFLOAT)(sMainDllBase + 0xB665D8);

        FLOAT cameraPitch = *(PFLOAT)(sMainDllBase + 0xB66390);
        //FLOAT cameraPitch = *(PFLOAT)(sMainDllBase + 0xB6659C);
        FLOAT cameraYaw = *(PFLOAT)(sMainDllBase + 0xB66394);

        FLOAT cameraDistance = *(PFLOAT)(sMainDllBase + 0xB663DC);
        FLOAT cameraHeight = *(PFLOAT)(sMainDllBase + 0xB663E0);

        playerX *= sScaleFactor;
        playerY *= sScaleFactor;
        playerZ *= sScaleFactor;

        //cameraX *= sScaleFactor;
        //cameraY *= sScaleFactor;
        //cameraZ *= sScaleFactor;

        FLOAT fov = *(PFLOAT)(sMainDllBase + 0xB663B0);

        VaDrawGrid({ playerX, playerY, playerZ }, 10.0f, 10, { 1.0f, 1.0f, 0.0f, 1.0f }); // TODO

        XMVECTOR playerPosition = XMVectorSet(playerX, playerY, playerZ, 0.0f);

        XMMATRIX scaling = XMMatrixScaling(10.0f, 10.0f, 10.0f);
        XMMATRIX rotationX = XMMatrixRotationX(0.0f);
        XMMATRIX rotationY = XMMatrixRotationY(0.0f);
        XMMATRIX rotationZ = XMMatrixRotationZ(0.0f);
        XMMATRIX rotation = rotationX * rotationY * rotationZ;
        XMMATRIX translation = XMMatrixTranslation(2072.646973f, -474.464447f, 1986.715576f);

        XMVECTOR rightDirection = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        XMVECTOR upDirection = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        XMVECTOR forwardDirection = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);

        printf("[%f %f] [%f %f %f]\n", cameraPitch, cameraYaw, playerX, playerY, playerZ);

        // First-Person camera
        //XMMATRIX rotationMatrix = XMMatrixRotationRollPitchYaw(cameraPitch * sPitchFactor, (cameraYaw + DEG_TO_RAD(sYawOffset)) * sYawFactor, 0.0f);
        //XMVECTOR lookAtVector = XMVector3TransformCoord(forwardDirection, rotationMatrix);
        //XMVECTOR upVector = XMVector3TransformCoord(upDirection, rotationMatrix);

        // Orbital camera
        FLOAT x = cameraDistance * sinf(sOrbitalPitch) * cosf(sOrbitalYaw);
        FLOAT y = cameraDistance * cosf(sOrbitalPitch);
        FLOAT z = cameraDistance * sinf(sOrbitalPitch) * sinf(sOrbitalYaw);
        XMVECTOR cameraPosition = XMVectorSet(x, y, z, 0.0f);

        XMMATRIX model = scaling * rotation * translation;
        //XMMATRIX view = XMMatrixLookAtLH(cameraPosition, cameraPosition + lookAtVector, upVector); // First-Person
        XMMATRIX view = XMMatrixLookAtLH(playerPosition + cameraPosition, playerPosition, upDirection); // Orbital
        XMMATRIX projection = XMMatrixPerspectiveFovLH(DEG_TO_RAD(fov), aspectRatio, 0.001f, 100000.0f);

        XMMATRIX cameraTranslation = XMMatrixTranslation(playerX + x, playerY + y, playerZ + z);
        XMMATRIX rotationPitch = XMMatrixRotationX(cameraPitch);
        XMMATRIX rotationYaw = XMMatrixRotationY(cameraYaw);

        XMVECTOR invCameraTranslationdeterminant;
        XMMATRIX invCameraTranslation = XMMatrixInverse(&invCameraTranslationdeterminant, cameraTranslation);

        view = view * rotationYaw;
        view = view * rotationPitch;

        gModelViewProjection.Model = XMMatrixTranspose(model);
        gModelViewProjection.View = XMMatrixTranspose(view);
        gModelViewProjection.Projection = XMMatrixTranspose(projection);

        COPY_INTO_CONSTANT_BUFFER(gModelViewProjectionBuffer, &gModelViewProjection, sizeof(MODEL_VIEW_PROJECTION));
    }
}

VOID VaRenderDirectX(VOID)
{
    // TODO
    VaDrawLine({ -10000.0f, 0.0f, 0.0f }, { 10000.0f, 0.0f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f });
    VaDrawLine({ 0.0f, -10000.0f, 0.0f }, { 0.0f, 10000.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f });
    VaDrawLine({ 0.0f, 0.0f, -10000.0f }, { 0.0f, 0.0f, 10000.0f }, { 0.0f, 0.0f, 1.0f, 1.0f });

    // TODO
    VaDrawGrid({ 0.0f, 0.0f, 0.0f }, 100.0f, 10, { 1.0f, 1.0f, 0.0f, 1.0f });
    VaDrawGrid({ 0.0f, 0.0f, 0.0f }, 10000.0f, 10, { 1.0f, 1.0f, 0.0f, 1.0f });

    VaRenderDefaultGeo();
    VaRenderLineBatch();
}
VOID VaRenderImGui(VOID)
{
    ImGui::BeginMainMenuBar();

    if (ImGui::BeginMenu("Debug"))
    {
        if (ImGui::MenuItem("Toggle Buffer Update"))
        {
            sAllowModelViewProjectionUpdate = !sAllowModelViewProjectionUpdate;
        }

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();

    ImGui::Begin("Debug");

    ImGui::DragFloat("Scale", &sScaleFactor, 0.01f, 0.0f, 10000.0f);
    ImGui::DragFloat("Pitch", &sPitchFactor, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat("Yaw", &sYawFactor, 0.01f, 0.0f, 4.0f);
    ImGui::DragFloat("YawOffset", &sYawOffset, 1.0f, -180.0f, 180.0f);
    ImGui::DragFloat("OrbitalPitch", &sOrbitalPitch, 0.01f, -XM_PI, XM_PI);
    ImGui::DragFloat("OrbitalYaw", &sOrbitalYaw, 0.01f, -XM_PI, XM_PI);

    ImGui::End();
}

UINT32 VaDetourPresent(IDXGISwapChain* SwapChain, UINT32 SyncInterval, UINT32 Flags)
{
    VaApplyGameplayFixes();

    if (!sPresentInitialized)
    {
        sPresentInitialized = TRUE;

        HR_CHECK(SwapChain->GetDevice(__uuidof(ID3D11Device), (PVOID*)&gDevice));

        gDevice->GetImmediateContext(&gDeviceContext);

        DXGI_SWAP_CHAIN_DESC swapChainDescription = { 0 };

        HR_CHECK(SwapChain->GetDesc(&swapChainDescription));

        sWindow = swapChainDescription.OutputWindow;
        sWindowProc = (WNDPROC)SetWindowLongPtrA(sWindow, GWLP_WNDPROC, (UINT64)VaWindowProc);

        ID3D11Texture2D* backBuffer = NULL;

        HR_CHECK(SwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (PVOID*)&backBuffer));

        HR_CHECK(gDevice->CreateRenderTargetView(backBuffer, NULL, &gMainRenderTargetView));

        backBuffer->Release();

        sImGuiContext = ImGui::CreateContext();

        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

        ImGui_ImplWin32_Init(sWindow);
        ImGui_ImplDX11_Init(gDevice, gDeviceContext);

        VaCreateLineBatchRenderer();
        VaCreateDefaultGeoRenderer();

        VaCreateConstantBuffers();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();

    ImGui::NewFrame();

    VaRenderImGui();

    ImGui::Render();

    gDeviceContext->OMSetRenderTargets(1, &gMainRenderTargetView, NULL);

    VaUpdateViewport();
    VaUpdateModelViewProjection();

    VaRenderDirectX();

    ImDrawData* drawData = ImGui::GetDrawData();

    ImGui_ImplDX11_RenderDrawData(drawData);

    return sPresentNew(SwapChain, SyncInterval, Flags);
}

LRESULT VaWindowProc(HWND Window, UINT Msg, WPARAM WParam, LPARAM LParam)
{
    ShowCursor(TRUE);

    if (ImGui_ImplWin32_WndProcHandler(Window, Msg, WParam, LParam))
    {
        return TRUE;
    }

    return DefWindowProcA(Window, Msg, WParam, LParam);
}

VOID VaCleanupDirectX(VOID)
{
    VaDestroyDefaultGeoRenderer();
    VaDestroyLineBatchRenderer();

    gModelViewProjectionBuffer->Release();
    gMainRenderTargetView->Release();
}
VOID VaCleanupWindow(VOID)
{
    SetWindowLongPtrA(sWindow, GWLP_WNDPROC, (UINT64)DefWindowProcA);
}
VOID VaCleanupImGui(VOID)
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();

    ImGui::DestroyContext(sImGuiContext);
}

INT32 WINAPI VaMainThread(PVOID UserParam)
{
    VaCreateConsole();

    sOkamiExeBase = VaFindModuleBase("okami.exe");
    sFlowerKernelDllBase = VaFindModuleBase("flower_kernel.dll");
    sMainDllBase = VaFindModuleBase("main.dll");

    sPresentOld = (PRESENT_PROC)VaGetPresentPointer();

    MH_Initialize();
    
    MH_CreateHook(sPresentOld, VaDetourPresent, (PVOID*)&sPresentNew);
    MH_EnableHook(sPresentOld);

    while (sRunning);

    MH_DisableHook(sPresentOld);
    MH_RemoveHook(sPresentOld);

    MH_Uninitialize();

    if (sPresentInitialized)
    {
        VaCleanupDirectX();
        VaCleanupWindow();
        VaCleanupImGui();
    }

    VaDestroyConsole();

    SetEvent(sMainThreadExitEvent);

    return 0;
}

/////////////////////////////////////////////////
// Entry Point
/////////////////////////////////////////////////

BOOL APIENTRY DllMain(HMODULE Module, UINT32 CallReason, PVOID Reserved)
{
    switch (CallReason)
    {
        case DLL_PROCESS_ATTACH:
        {
            sMainThreadExitEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
            sMainThread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)VaMainThread, Module, 0, NULL);

            break;
        }
        case DLL_PROCESS_DETACH:
        {
            sRunning = FALSE;

            WaitForSingleObject(sMainThreadExitEvent, INFINITE);

            CloseHandle(sMainThread);
            CloseHandle(sMainThreadExitEvent);
        }
    }

    return TRUE;
}