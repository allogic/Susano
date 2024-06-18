#pragma once

#include <d3d11.h>
#include <directxmath.h>

using namespace DirectX;

/////////////////////////////////////////////////
// Type Definition
/////////////////////////////////////////////////

struct MODEL_VIEW_PROJECTION
{
    XMMATRIX Model;
    XMMATRIX View;
    XMMATRIX Projection;
};

/////////////////////////////////////////////////
// Global Variables
/////////////////////////////////////////////////

extern ID3D11Device* gDevice;
extern ID3D11DeviceContext* gDeviceContext;
extern ID3D11RenderTargetView* gMainRenderTargetView;
extern ID3D11Buffer* gModelViewProjectionBuffer;

extern MODEL_VIEW_PROJECTION gModelViewProjection;