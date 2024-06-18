#pragma once

#include <windows.h>

#include <directxmath.h>

using namespace DirectX;

VOID VaCreateLineBatchRenderer(VOID);
VOID VaDestroyLineBatchRenderer(VOID);

VOID VaDrawLine(XMFLOAT3 A, XMFLOAT3 B, XMFLOAT4 C);
VOID VaDrawBox(XMFLOAT3 P, XMFLOAT3 S, XMFLOAT4 C);
VOID VaDrawGrid(XMFLOAT3 P, FLOAT S, UINT32 N, XMFLOAT4 C);

VOID VaRenderLineBatch(VOID);