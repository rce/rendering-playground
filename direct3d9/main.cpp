#include <iostream>
#include <fstream>
#include <Windows.h>
#include <filesystem>
#include <vector>
#include <memory>
#include <d3d9.h>
#pragma comment (lib, "d3d9.lib")
#include <DirectXMath.h>

void PrintLastError()
{
	auto error = GetLastError();
	if (error != 0)
	{
		LPSTR messageBuffer = nullptr;
		auto size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, NULL);
		std::cerr << "GetLastError() indicates: " << messageBuffer << std::endl;
		LocalFree(messageBuffer);
	}
}

#define ASSERT(expr) do { \
	if (!(expr)) { \
		std::cerr << "Error: " << #expr << " is false" << std::endl; \
		PrintLastError(); \
		throw std::exception(#expr " is false"); \
	} \
} while (0)

template<typename T>
void SafeRelease(T*& ptr)
{
	if (ptr != nullptr)
	{
		ptr->Release();
		ptr = nullptr;
	}
}

struct CUSTOMVERTEX { D3DVECTOR xyz; D3DVECTOR normal; };
#define CUSTOMFVF (D3DFVF_XYZ | D3DFVF_NORMAL)

class Model {
public:
	int32_t numFaces = 0;
	std::vector<CUSTOMVERTEX> vertices;
	std::vector<int32_t> indices;
	static Model FromBinarySTL(const std::filesystem::path& filename)
	{
		ASSERT(std::filesystem::exists(filename));
		std::ifstream f(filename, std::ios::binary);

#pragma pack(push, r1, 1)
		struct STL_TRIANGLE {
			D3DVECTOR normal;
			D3DVECTOR vertices[3];
			uint16_t attributeByteCount;
		};
#pragma pack(pop, r1)
		static_assert(sizeof(STL_TRIANGLE) == 50, "STL_TRIANGLE size mismatch");

		std::vector<D3DVECTOR> normals;
		std::vector<CUSTOMVERTEX> vertices;
		std::vector<int32_t> indices;
		int32_t numFaces = 0;

		uint8_t header[80];
		f.read(reinterpret_cast<char*>(&header), sizeof(header));

		uint32_t numTriangles;
		f.read(reinterpret_cast<char*>(&numTriangles), sizeof(numTriangles));
		ASSERT(numTriangles > 0);

		for (size_t i = 0; i < numTriangles; ++i)
		{
			STL_TRIANGLE t;
			f.read(reinterpret_cast<char*>(&t), sizeof(STL_TRIANGLE));
			for (auto& v : t.vertices)
			{
				vertices.push_back({ v, t.normal });
				indices.push_back(vertices.size() - 1);
			}
		}

		return Model(vertices, indices, numTriangles);
	}

	Model(Model& other) = default;
	Model(Model&& other) = default;
	Model(std::vector<CUSTOMVERTEX> vertices, std::vector<int32_t> indices, int32_t numFaces)
		: numFaces(numFaces), vertices(vertices), indices(indices)
	{
	}

	~Model()
	{
	}
};

class VertexBuffer
{
	const LPDIRECT3DDEVICE9 pDevice;
	LPDIRECT3DVERTEXBUFFER9 pBuffer;

public:
	template <typename vertex_t>
	VertexBuffer(const LPDIRECT3DDEVICE9 pDevice, const std::vector<vertex_t>& vs, const DWORD FVF)
		: pDevice(pDevice)
	{
		ASSERT(SUCCEEDED(pDevice->CreateVertexBuffer(vs.size() * sizeof(vertex_t), 0, FVF, D3DPOOL_MANAGED, &pBuffer, NULL)));
		Write(vs);
	};
	~VertexBuffer()
	{
		SafeRelease(pBuffer);
	}

	void Write(const void* data, size_t size)
	{

		VOID* pDest;
		ASSERT(SUCCEEDED(pBuffer->Lock(0, size, (void**)&pDest, 0)));
		memcpy(pDest, data, size);
		ASSERT(SUCCEEDED(pBuffer->Unlock()));
	}

	template <typename vertex_t>
	void Write(const std::vector<vertex_t>& vs)
	{
		return Write(vs.data(), vs.size() * sizeof(vertex_t));
	};
	const LPDIRECT3DVERTEXBUFFER9 Buffer() { return pBuffer; }
};

class IndexBuffer
{
	const LPDIRECT3DDEVICE9 pDevice;
	LPDIRECT3DINDEXBUFFER9 pBuffer;

public:
	template <typename T>
	IndexBuffer(const LPDIRECT3DDEVICE9 pDevice, const std::vector<T>& is, const D3DFORMAT Format)
		: pDevice(pDevice)
	{
		ASSERT(SUCCEEDED(pDevice->CreateIndexBuffer(is.size() * sizeof(T), 0, Format, D3DPOOL_MANAGED, &pBuffer, NULL)));
		Write(is);
	};
	~IndexBuffer()
	{
		SafeRelease(pBuffer);
	}

	void Write(const void* data, size_t size)
	{
		VOID* pDest;
		ASSERT(SUCCEEDED(pBuffer->Lock(0, size, (void**)&pDest, 0)));
		memcpy(pDest, data, size);
		ASSERT(SUCCEEDED(pBuffer->Unlock()));
	}
	template <typename T>
	void Write(const std::vector<T>& is)
	{
		return Write(is.data(), is.size() * sizeof(T));
	}
	const LPDIRECT3DINDEXBUFFER9 Buffer() { return pBuffer; }
};

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

enum WindowType { FULLSCREEN, WINDOWED };

class Window
{
	LPCSTR windowClassName = "WindowClass";
	HWND hWnd;
	HINSTANCE hInstance;
	LPDIRECT3D9 pD3D = nullptr;
	LPDIRECT3DDEVICE9 pDevice = nullptr;
	std::pair<int, int> resolution = { 3840, 2160 };
	Model teapot;

public:
	Window(WindowType windowType)
		: hInstance(GetModuleHandle(NULL))
		, teapot(LoadModel("teapot.stl"))
	{
		ASSERT(this->hInstance != NULL);
		WNDCLASSEX wc;
		ZeroMemory(&wc, sizeof(WNDCLASSEX));
		wc.cbSize = sizeof(WNDCLASSEX);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = WindowProc;
		wc.hInstance = this->hInstance;
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		ASSERT(wc.hCursor != NULL);
		wc.hbrBackground = (HBRUSH)COLOR_WINDOW;
		wc.lpszClassName = this->windowClassName;
		ASSERT(RegisterClassEx(&wc) != 0);

		RECT rect;
		ASSERT(GetWindowRect(GetDesktopWindow(), &rect) != 0);
		auto width = rect.right - rect.left, height = rect.bottom - rect.top;

		if (windowType == FULLSCREEN)
		{
			// This is actually "windowed fullscreen" or "borderless windowed" mode
			this->hWnd = CreateWindowEx(NULL, wc.lpszClassName, "Direct3D Playground", WS_POPUP, 0, 0, width, height, NULL, NULL, this->hInstance, NULL);
		}
		else if (windowType == WINDOWED)
		{
			this->hWnd = CreateWindowEx(NULL, wc.lpszClassName, "Direct3D Playground", WS_OVERLAPPEDWINDOW, width / 4, height / 4, width / 2, height / 2, NULL, NULL, this->hInstance, NULL);
		}
		ASSERT(this->hWnd != NULL);

		SetLastError(0);
		if (SetWindowLongPtr(this->hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this)) == 0)
		{
			ASSERT(GetLastError() == 0);
		}

		this->InitDirect3D9();
	}

	Model LoadModel(const std::string& filename)
	{
		std::filesystem::path repo_root = "../../../..";
		auto models_dir = repo_root / "models";
		std::cout << std::filesystem::absolute(repo_root) << std::endl;
		return Model::FromBinarySTL(models_dir / filename);
	}


	~Window()
	{
		std::cerr << "Window::~Window()" << std::endl;
		if (this->hWnd != NULL)
		{
			ASSERT(DestroyWindow(this->hWnd));
		}
		ASSERT(UnregisterClass(this->windowClassName, this->hInstance));
	}

	void CleanupDirect3D9()
	{
		SafeRelease(this->pDevice);
		SafeRelease(this->pD3D);
	}

	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
		case WM_SIZE:
			OnResize(LOWORD(lParam), HIWORD(lParam));
			break;
		case WM_DESTROY:
			this->hWnd = NULL;
			PostQuitMessage(EXIT_SUCCESS);
			return 0;
		}

		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	int ShowAndRun()
	{
		ShowWindow(this->hWnd, SW_SHOW);
		MSG msg;
		int result = 0;
		bool quit = false;
		while (!quit)
		{
			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
				if (msg.message == WM_QUIT)
				{
					quit = true;
					result = static_cast<int>(msg.wParam);
				}
			}

			this->Render();
		}
		this->CleanupDirect3D9();
		return result;
	}

	void InitDirect3D9()
	{
		this->pD3D = Direct3DCreate9(D3D_SDK_VERSION);
		auto d3dpp = MakeD3DPresentParams();
		ASSERT(SUCCEEDED(pD3D->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, this->hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &this->pDevice)));
		AfterReset(this->pDevice);
	}

	void AfterReset(LPDIRECT3DDEVICE9 pDevice) {
		ASSERT(SUCCEEDED(pDevice->SetRenderState(D3DRS_ZENABLE, TRUE)));
		InitLights(pDevice);
	};

	D3DPRESENT_PARAMETERS MakeD3DPresentParams()
	{
		D3DPRESENT_PARAMETERS present;
		ZeroMemory(&present, sizeof(present));
		present.Windowed = TRUE;
		present.hDeviceWindow = hWnd;
		present.BackBufferFormat = D3DFMT_X8R8G8B8;
		present.SwapEffect = D3DSWAPEFFECT_DISCARD;
		present.EnableAutoDepthStencil = TRUE;
		present.AutoDepthStencilFormat = D3DFMT_D16;
		present.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
		present.FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;
		return present;
	}

	void OnResize(int width, int height)
	{
		if (pDevice)
		{
			resolution = std::make_pair(width, height);
			auto d3dpp = MakeD3DPresentParams();
			ASSERT(SUCCEEDED(pDevice->Reset(&d3dpp)));
			AfterReset(pDevice);
		}
		UpdateWindow(hWnd);
	}

	void InitLights(LPDIRECT3DDEVICE9 pDevice)
	{
		ASSERT(SUCCEEDED(pDevice->SetRenderState(D3DRS_LIGHTING, TRUE)));
		ASSERT(SUCCEEDED(pDevice->SetRenderState(D3DRS_AMBIENT, D3DCOLOR_XRGB(50, 50, 50))));

		D3DLIGHT9 light;
		ZeroMemory(&light, sizeof(light));
		light.Type = D3DLIGHT_DIRECTIONAL;
		light.Diffuse = D3DCOLORVALUE{ 0.5f, 0.5f, 0.5f, 1.0f };
		light.Direction = D3DVECTOR{ -1.0f, -0.3f, -1.0f };

		ASSERT(SUCCEEDED(pDevice->SetLight(0, &light)));
		ASSERT(SUCCEEDED(pDevice->LightEnable(0, TRUE)));

		D3DMATERIAL9 material;
		ZeroMemory(&material, sizeof(material));
		material.Diffuse = D3DCOLORVALUE{ 1.0f, 1.0f, 1.0f, 1.0f };
		material.Ambient = D3DCOLORVALUE{ 1.0f, 1.0f, 1.0f, 1.0f };
		ASSERT(SUCCEEDED(pDevice->SetMaterial(&material)));
	}

	void SetTransform(DirectX::XMFLOAT3 position)
	{
		SetTransform(D3DTS_WORLD, DirectX::XMMatrixTranslation(position.x, position.y, position.z));
	}

	HRESULT SetTransform(D3DTRANSFORMSTATETYPE State, DirectX::XMMATRIX matrix)
	{
		DirectX::XMFLOAT4X4 mat;
		DirectX::XMStoreFloat4x4(&mat, matrix);
		ASSERT(SUCCEEDED(pDevice->SetTransform(State, reinterpret_cast<D3DMATRIX*>(&mat))));
	}

	void Render()
	{
		ASSERT(SUCCEEDED(pDevice->Clear(0, NULL, D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 0, 0), 1.0f, 0)));
		ASSERT(SUCCEEDED(pDevice->Clear(0, NULL, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 50, 100), 1.0f, 0)));
		ASSERT(SUCCEEDED(pDevice->BeginScene()));
		ASSERT(SUCCEEDED(pDevice->SetFVF(CUSTOMFVF)));

		auto world = DirectX::XMMatrixIdentity();
		SetTransform(D3DTS_WORLD, world);

		RenderModel(this->teapot);

		SetTransform(D3DTS_VIEW, ViewMatrix());
		SetTransform(D3DTS_PROJECTION, ProjectionMatrix());

		ASSERT(SUCCEEDED(pDevice->EndScene()));

		auto presentResult = pDevice->Present(NULL, NULL, NULL, NULL);
		if (presentResult == D3DERR_DEVICELOST) {
			std::cout << "Present returned D3DERR_DEVICELOST" << std::endl;
		}

		ASSERT(SUCCEEDED(presentResult));
	}

	float AspectRatio()
	{
		return static_cast<float>(resolution.first) / static_cast<float>(resolution.second);
	}

	DirectX::XMMATRIX ViewMatrix()
	{
		auto eyePosition = DirectX::XMFLOAT3(10.0f, 10.0f, 10.0f);
		auto focusPosition = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

		auto upVector = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 1.0f);
		return DirectX::XMMatrixLookAtLH(
			DirectX::XMLoadFloat3(&eyePosition),
			DirectX::XMLoadFloat3(&focusPosition),
			upVector
		);
	}

	DirectX::XMMATRIX ProjectionMatrix()
	{
		auto fov = 80.0f;
		auto zNear = 0.1f, zFar = 100.0f;
		return DirectX::XMMatrixPerspectiveFovLH(DirectX::XMConvertToRadians(fov), AspectRatio(), zNear, zFar);
	}

	void RenderModel(const Model& model)
	{

		// TODO: Don't reallocate buffers on every frame
		auto pVertexbuffer = std::make_unique<VertexBuffer>(pDevice, model.vertices, CUSTOMFVF);
		auto pIndexBuffer = std::make_unique<IndexBuffer>(pDevice, model.indices, D3DFMT_INDEX32);
		ASSERT(SUCCEEDED(pDevice->SetFVF(CUSTOMFVF)));

		ASSERT(SUCCEEDED(pDevice->SetIndices(pIndexBuffer->Buffer())));
		ASSERT(SUCCEEDED(pDevice->SetStreamSource(0, pVertexbuffer->Buffer(), 0, sizeof(CUSTOMVERTEX))));

		INT BaseVertexIndex = 0;
		UINT MinVertexIndex = 0;
		UINT NumVertices = model.vertices.size();
		UINT startIndex = 0;
		UINT primCount = model.numFaces;
		ASSERT(SUCCEEDED(pDevice->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, BaseVertexIndex, MinVertexIndex, NumVertices, startIndex, primCount)));
	}
};

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	auto pThis = reinterpret_cast<Window*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));
	if (pThis)
	{
		return pThis->WndProc(hWnd, message, wParam, lParam);
	}
	else
	{
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
}

int main()
{
	Window window(WindowType::WINDOWED);
	return window.ShowAndRun();
}
