#include <stdio.h>
#include <string.h>

#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>

#include "susano.h"
#include "linebatchrenderer.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "d3dcompiler.lib")

/////////////////////////////////////////////////
// Macros
/////////////////////////////////////////////////

#define PRINT_LAST_ERROR() printf("0x%08X\n", GetLastError())

#define ARRAY_LENGTH(ARRAY) (sizeof(ARRAY) / sizeof((ARRAY)[0]))

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

struct VERTEX
{
	XMFLOAT3 Position;
	XMFLOAT4 Color;
};

/////////////////////////////////////////////////
// Local Variables
/////////////////////////////////////////////////

static ID3DBlob* sVertexShaderBlob = NULL;
static ID3DBlob* sPixelShaderBlob = NULL;

static ID3D11VertexShader* sVertexShader = NULL;
static ID3D11PixelShader* sPixelShader = NULL;
static ID3D11InputLayout* sInputLayout = NULL;
static ID3D11Buffer* sVertexBuffer = NULL;
static ID3D11Buffer* sIndexBuffer = NULL;

static CHAR sVertexShaderSource[] = R"hlsl(
	cbuffer ModelViewProjection : register(b0)
	{
		matrix model;
		matrix view;
		matrix projection;
	};

	struct VS_INPUT
	{
		float3 position : POSITION;
		float4 color : COLOR;
	};
	
	struct PS_INPUT
	{
		float4 position : SV_POSITION;
		float4 color : COLOR;
	};
	
	PS_INPUT VS(VS_INPUT input)
	{
		PS_INPUT output;

		float4 position = float4(input.position, 1.0f);

		position = mul(position, model);
		position = mul(position, view);
		position = mul(position, projection);

		output.position = position;
		output.color = input.color;

		return output;
	}
)hlsl";

static CHAR sPixelShaderSource[] = R"hlsl(
	struct PS_INPUT
	{
		float4 position : SV_POSITION;
		float4 color : COLOR;
	};

	float4 PS(PS_INPUT input) : SV_TARGET
	{
		return input.color;
	}
)hlsl";

static D3D11_INPUT_ELEMENT_DESC sInputLayoutSource[] =
{
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
};

static VERTEX sVertices[] =
{
	{ XMFLOAT3{ -1.0f, -1.0f, -1.0f }, XMFLOAT4{ 1.0f, 0.0f, 0.0f, 1.0f } },
	{ XMFLOAT3{  1.0f, -1.0f, -1.0f }, XMFLOAT4{ 1.0f, 0.0f, 0.0f, 1.0f } },
	{ XMFLOAT3{  1.0f,  1.0f, -1.0f }, XMFLOAT4{ 1.0f, 0.0f, 0.0f, 1.0f } },
	{ XMFLOAT3{ -1.0f,  1.0f, -1.0f }, XMFLOAT4{ 1.0f, 0.0f, 0.0f, 1.0f } },
	{ XMFLOAT3{ -1.0f, -1.0f,  1.0f }, XMFLOAT4{ 1.0f, 0.0f, 0.0f, 1.0f } },
	{ XMFLOAT3{  1.0f, -1.0f,  1.0f }, XMFLOAT4{ 1.0f, 0.0f, 0.0f, 1.0f } },
	{ XMFLOAT3{  1.0f,  1.0f,  1.0f }, XMFLOAT4{ 1.0f, 0.0f, 0.0f, 1.0f } },
	{ XMFLOAT3{ -1.0f,  1.0f,  1.0f }, XMFLOAT4{ 1.0f, 0.0f, 0.0f, 1.0f } },
};

static UINT16 sIndices[] =
{
	0, 1, 2, 0, 2, 3,
	4, 5, 6, 4, 6, 7,
	4, 0, 3, 4, 3, 7,
	1, 5, 6, 1, 6, 2,
	3, 2, 6, 3, 6, 7,
	4, 5, 1, 4, 1, 0,
};

/////////////////////////////////////////////////
// Function Definition
/////////////////////////////////////////////////

static VOID VaCreateShaders(VOID);
static VOID VaCreateVertexBuffers(VOID);
static VOID VaCreateIndexBuffers(VOID);
static VOID VaCreateInputLayouts(VOID);

/////////////////////////////////////////////////
// Function Implementation
/////////////////////////////////////////////////

VOID VaCreateDefaultGeoRenderer(VOID)
{
	VaCreateShaders();
	VaCreateVertexBuffers();
	VaCreateIndexBuffers();
	VaCreateInputLayouts();
}
VOID VaDestroyDefaultGeoRenderer(VOID)
{
	sVertexShaderBlob->Release();
	sPixelShaderBlob->Release();

	sVertexShader->Release();
	sPixelShader->Release();
	sInputLayout->Release();
	sVertexBuffer->Release();
	sIndexBuffer->Release();
}

VOID VaRenderDefaultGeo(VOID)
{
	//gModelViewProjection.Model // TODO

	COPY_INTO_CONSTANT_BUFFER(sVertexBuffer, sVertices, sizeof(VERTEX) * ARRAY_LENGTH(sVertices));
	COPY_INTO_CONSTANT_BUFFER(sIndexBuffer, sIndices, sizeof(UINT16) * ARRAY_LENGTH(sIndices));
	COPY_INTO_CONSTANT_BUFFER(gModelViewProjectionBuffer, &gModelViewProjection, sizeof(MODEL_VIEW_PROJECTION));

	UINT32 stride = sizeof(VERTEX);
	UINT32 offset = 0;

	gDeviceContext->IASetInputLayout(sInputLayout);
	gDeviceContext->VSSetShader(sVertexShader, NULL, 0);
	gDeviceContext->VSSetConstantBuffers(0, 1, &gModelViewProjectionBuffer);
	gDeviceContext->PSSetShader(sPixelShader, NULL, 0);
	gDeviceContext->IASetVertexBuffers(0, 1, &sVertexBuffer, &stride, &offset);
	gDeviceContext->IASetIndexBuffer(sIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
	gDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	gDeviceContext->DrawIndexed(ARRAY_LENGTH(sIndices), 0, 0);
}

static VOID VaCreateShaders(VOID)
{
	HR_CHECK(D3DCompile(sVertexShaderSource, ARRAY_LENGTH(sVertexShaderSource), NULL, NULL, NULL, "VS", "vs_4_0", 0, 0, &sVertexShaderBlob, NULL));
	HR_CHECK(D3DCompile(sPixelShaderSource, ARRAY_LENGTH(sPixelShaderSource), NULL, NULL, NULL, "PS", "ps_4_0", 0, 0, &sPixelShaderBlob, NULL));

	HR_CHECK(gDevice->CreateVertexShader(sVertexShaderBlob->GetBufferPointer(), sVertexShaderBlob->GetBufferSize(), NULL, &sVertexShader));
	HR_CHECK(gDevice->CreatePixelShader(sPixelShaderBlob->GetBufferPointer(), sPixelShaderBlob->GetBufferSize(), NULL, &sPixelShader));
}
static VOID VaCreateVertexBuffers(VOID)
{
	D3D11_BUFFER_DESC bufferDescription = { 0 };
	bufferDescription.Usage = D3D11_USAGE_DYNAMIC;
	bufferDescription.ByteWidth = sizeof(VERTEX) * ARRAY_LENGTH(sVertices);
	bufferDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HR_CHECK(gDevice->CreateBuffer(&bufferDescription, NULL, &sVertexBuffer));
}
static VOID VaCreateIndexBuffers(VOID)
{
	D3D11_BUFFER_DESC bufferDescription = { 0 };
	bufferDescription.Usage = D3D11_USAGE_DYNAMIC;
	bufferDescription.ByteWidth = sizeof(UINT16) * ARRAY_LENGTH(sIndices);
	bufferDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HR_CHECK(gDevice->CreateBuffer(&bufferDescription, NULL, &sIndexBuffer));
}
static VOID VaCreateInputLayouts(VOID)
{
	HR_CHECK(gDevice->CreateInputLayout(sInputLayoutSource, ARRAY_LENGTH(sInputLayoutSource), sVertexShaderBlob->GetBufferPointer(), sVertexShaderBlob->GetBufferSize(), &sInputLayout));
}