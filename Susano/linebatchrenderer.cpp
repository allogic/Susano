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

#define VERTEX_BUFFER_SIZE (65535)
#define INDEX_BUFFER_SIZE (VERTEX_BUFFER_SIZE * 2)

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

static VERTEX* sVertices = NULL;
static UINT16* sIndices = NULL;

static UINT16 sVertexOffset = 0;
static UINT16 sIndexOffset = 0;

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

VOID VaCreateLineBatchRenderer(VOID)
{
	VaCreateShaders();
	VaCreateVertexBuffers();
	VaCreateIndexBuffers();
	VaCreateInputLayouts();

	sVertices = (VERTEX*)malloc(sizeof(VERTEX) * VERTEX_BUFFER_SIZE);
	sIndices = (UINT16*)malloc(sizeof(UINT16) * INDEX_BUFFER_SIZE);
}
VOID VaDestroyLineBatchRenderer(VOID)
{
	sVertexShaderBlob->Release();
	sPixelShaderBlob->Release();

	sVertexShader->Release();
	sPixelShader->Release();
	sInputLayout->Release();
	sVertexBuffer->Release();
	sIndexBuffer->Release();
}

VOID VaDrawLine(XMFLOAT3 A, XMFLOAT3 B, XMFLOAT4 C)
{
	sVertices[sVertexOffset + 0].Position = A;
	sVertices[sVertexOffset + 1].Position = B;

	sVertices[sVertexOffset + 0].Color = C;
	sVertices[sVertexOffset + 1].Color = C;

	sIndices[sIndexOffset + 0] = sVertexOffset + 0;
	sIndices[sIndexOffset + 1] = sVertexOffset + 1;

	sVertexOffset += 2;
	sIndexOffset += 2;
}
VOID VaDrawBox(XMFLOAT3 P, XMFLOAT3 S, XMFLOAT4 C)
{
	XMFLOAT3 hs(S.x / 2.0f, S.y / 2.0f, S.z / 2.0f);

	sVertices[sVertexOffset + 0].Position = XMFLOAT3(P.x - hs.x, P.y - hs.y, P.z - hs.z);
	sVertices[sVertexOffset + 1].Position = XMFLOAT3(P.x + hs.x, P.y - hs.y, P.z - hs.z);
	sVertices[sVertexOffset + 2].Position = XMFLOAT3(P.x - hs.x, P.y + hs.y, P.z - hs.z);
	sVertices[sVertexOffset + 3].Position = XMFLOAT3(P.x + hs.x, P.y + hs.y, P.z - hs.z);

	sVertices[sVertexOffset + 4].Position = XMFLOAT3(P.x - hs.x, P.y - hs.y, P.z + hs.z);
	sVertices[sVertexOffset + 5].Position = XMFLOAT3(P.x + hs.x, P.y - hs.y, P.z + hs.z);
	sVertices[sVertexOffset + 6].Position = XMFLOAT3(P.x - hs.x, P.y + hs.y, P.z + hs.z);
	sVertices[sVertexOffset + 7].Position = XMFLOAT3(P.x + hs.x, P.y + hs.y, P.z + hs.z);

	sVertices[sVertexOffset + 0].Color = C;
	sVertices[sVertexOffset + 1].Color = C;
	sVertices[sVertexOffset + 2].Color = C;
	sVertices[sVertexOffset + 3].Color = C;

	sVertices[sVertexOffset + 4].Color = C;
	sVertices[sVertexOffset + 5].Color = C;
	sVertices[sVertexOffset + 6].Color = C;
	sVertices[sVertexOffset + 7].Color = C;

	sIndices[sIndexOffset + 0] = sVertexOffset + 0;
	sIndices[sIndexOffset + 1] = sVertexOffset + 1;
	sIndices[sIndexOffset + 2] = sVertexOffset + 0;
	sIndices[sIndexOffset + 3] = sVertexOffset + 2;
	sIndices[sIndexOffset + 4] = sVertexOffset + 2;
	sIndices[sIndexOffset + 5] = sVertexOffset + 3;

	sIndices[sIndexOffset + 6] = sVertexOffset + 3;
	sIndices[sIndexOffset + 7] = sVertexOffset + 1;
	sIndices[sIndexOffset + 8] = sVertexOffset + 4;
	sIndices[sIndexOffset + 9] = sVertexOffset + 5;
	sIndices[sIndexOffset + 10] = sVertexOffset + 4;
	sIndices[sIndexOffset + 11] = sVertexOffset + 6;

	sIndices[sIndexOffset + 12] = sVertexOffset + 6;
	sIndices[sIndexOffset + 13] = sVertexOffset + 7;
	sIndices[sIndexOffset + 14] = sVertexOffset + 7;
	sIndices[sIndexOffset + 15] = sVertexOffset + 5;
	sIndices[sIndexOffset + 16] = sVertexOffset + 0;
	sIndices[sIndexOffset + 17] = sVertexOffset + 4;

	sIndices[sIndexOffset + 18] = sVertexOffset + 1;
	sIndices[sIndexOffset + 19] = sVertexOffset + 5;
	sIndices[sIndexOffset + 20] = sVertexOffset + 2;
	sIndices[sIndexOffset + 21] = sVertexOffset + 6;
	sIndices[sIndexOffset + 22] = sVertexOffset + 3;
	sIndices[sIndexOffset + 23] = sVertexOffset + 7;

	sVertexOffset += 8;
	sIndexOffset += 24;
}
VOID VaDrawGrid(XMFLOAT3 P, FLOAT S, UINT32 N, XMFLOAT4 C)
{
	UINT32 indexOffset = 0;

	FLOAT ss = S / N;
	FLOAT hs = S / 2.0F;

	for (UINT32 i = 0; i <= N; i++)
	{
		FLOAT go = ((FLOAT)i) * ss - hs;

		sVertices[sVertexOffset + indexOffset + 0].Position = XMFLOAT3(P.x + go, P.y, P.z - hs);
		sVertices[sVertexOffset + indexOffset + 1].Position = XMFLOAT3(P.x + go, P.y, P.z + hs);
		sVertices[sVertexOffset + indexOffset + 2].Position = XMFLOAT3(P.x - hs, P.y, P.z + go);
		sVertices[sVertexOffset + indexOffset + 3].Position = XMFLOAT3(P.x + hs, P.y, P.z + go);

		sVertices[sVertexOffset + indexOffset + 0].Color = C;
		sVertices[sVertexOffset + indexOffset + 1].Color = C;
		sVertices[sVertexOffset + indexOffset + 2].Color = C;
		sVertices[sVertexOffset + indexOffset + 3].Color = C;

		sIndices[sIndexOffset + indexOffset + 0] = sVertexOffset + indexOffset + 0;
		sIndices[sIndexOffset + indexOffset + 1] = sVertexOffset + indexOffset + 1;
		sIndices[sIndexOffset + indexOffset + 2] = sVertexOffset + indexOffset + 2;
		sIndices[sIndexOffset + indexOffset + 3] = sVertexOffset + indexOffset + 3;

		indexOffset += 4;
	}

	sVertexOffset += indexOffset;
	sIndexOffset += indexOffset;
}

VOID VaRenderLineBatch(VOID)
{
	//gModelViewProjection.Model // TODO

	COPY_INTO_CONSTANT_BUFFER(sVertexBuffer, sVertices, sizeof(VERTEX) * sVertexOffset);
	COPY_INTO_CONSTANT_BUFFER(sIndexBuffer, sIndices, sizeof(UINT16) * sIndexOffset);
	COPY_INTO_CONSTANT_BUFFER(gModelViewProjectionBuffer, &gModelViewProjection, sizeof(MODEL_VIEW_PROJECTION));

	UINT32 stride = sizeof(VERTEX);
	UINT32 offset = 0;

	gDeviceContext->IASetInputLayout(sInputLayout);
	gDeviceContext->VSSetShader(sVertexShader, NULL, 0);
	gDeviceContext->VSSetConstantBuffers(0, 1, &gModelViewProjectionBuffer);
	gDeviceContext->PSSetShader(sPixelShader, NULL, 0);
	gDeviceContext->IASetVertexBuffers(0, 1, &sVertexBuffer, &stride, &offset);
	gDeviceContext->IASetIndexBuffer(sIndexBuffer, DXGI_FORMAT_R16_UINT, 0);
	gDeviceContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

	gDeviceContext->DrawIndexed(sIndexOffset, 0, 0);

	sVertexOffset = 0;
	sIndexOffset = 0;
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
	bufferDescription.ByteWidth = sizeof(VERTEX) * VERTEX_BUFFER_SIZE;
	bufferDescription.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HR_CHECK(gDevice->CreateBuffer(&bufferDescription, NULL, &sVertexBuffer));
}
static VOID VaCreateIndexBuffers(VOID)
{
	D3D11_BUFFER_DESC bufferDescription = { 0 };
	bufferDescription.Usage = D3D11_USAGE_DYNAMIC;
	bufferDescription.ByteWidth = sizeof(UINT16) * INDEX_BUFFER_SIZE;
	bufferDescription.BindFlags = D3D11_BIND_INDEX_BUFFER;
	bufferDescription.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

	HR_CHECK(gDevice->CreateBuffer(&bufferDescription, NULL, &sIndexBuffer));
}
static VOID VaCreateInputLayouts(VOID)
{
	HR_CHECK(gDevice->CreateInputLayout(sInputLayoutSource, ARRAY_LENGTH(sInputLayoutSource), sVertexShaderBlob->GetBufferPointer(), sVertexShaderBlob->GetBufferSize(), &sInputLayout));
}