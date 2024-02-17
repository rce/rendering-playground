#include <iostream>
#include <Windows.h>

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


LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

enum WindowType { FULLSCREEN, WINDOWED };

class Window
{
	LPCSTR windowClassName = "WindowClass";
	HWND hWnd;
	HINSTANCE hInstance;
public:
	Window(WindowType windowType)
		: hInstance(GetModuleHandle(NULL))
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

	LRESULT WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		switch (message)
		{
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
		}
		return result;
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
