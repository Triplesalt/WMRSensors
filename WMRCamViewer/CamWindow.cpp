//outputs the camera stream using OpenGL
#include "stdafx.h"

#include "CamWindow.h"

#include "include\GL\glew.h"
#include "include\GL\wglew.h"
#include "include\glm\glm.hpp"
#include "include\glm\gtc\type_ptr.hpp"
#pragma comment(lib, "glew32.lib")
#pragma comment(lib, "OpenGL32.lib")
#pragma comment(lib, "glu32.lib")

#include <time.h>
#include <vector>

INT_PTR CALLBACK CameraWindowProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

struct vertex2D
{
	float x,y;
	float u,v;
};
const vertex2D frameVertices1[] = {
	{-1,-1, 0,1}, //bottom left 1
	{0,-1, 1,1}, //bottom right 1
	{0,1, 1,0}, //top right 1
	{-1,1, 0,0}, //top left 1
};
const BYTE frameIndices1[] = {
	0, 1, 2, 2, 3, 0, //1
};
const vertex2D frameVertices2[] = {
	{0,-1, 0,1}, //bottom left 2
	{1,-1, 1,1}, //bottom right 2
	{1,1, 1,0}, //top right 2
	{0,1, 0,0}, //top left 2
};
const BYTE frameIndices2[] = {
	0, 1, 2, 2, 3, 0, //2
};
const GLchar *faceVertexShader1 = 
"#version 150\r\n \
precision highp float;\r\n \
in vec2 _in_vec_pos;\r\n \
in vec2 _in_tex_pos;\r\n \
\r\n \
uniform mat4 _in_ProjMat;\r\n \
\r\n \
out __perVertex {\r\n \
	 vec2 tex_inf;\r\n \
};\r\n \
\r\n \
void main() {\r\n \
	tex_inf = _in_tex_pos;\r\n \
	gl_Position = _in_ProjMat * vec4(_in_vec_pos,0.0,1.0);\r\n \
}\r\n";

const GLchar *faceFragmentShader1 = 
"#version 150 core\r\n\
precision highp float;\r\n\
out vec4 fragColor;\r\n\
\r\n\
in __perVertex {\r\n\
	vec2 tex_inf;\r\n\
};\r\n\
uniform sampler2D _in_tex;\r\n\
\r\n\
void main() {\r\n\
		float value = texture2D(_in_tex, tex_inf).r;\r\n\
		fragColor = vec4(value, value, value, 1);\r\n\
}\r\n\0);\r\n\
";

class CamWindow
{
public:
	WORD camID;
	BYTE curTextures[2][640*480];

	HDC hDC;
	HGLRC hGLRC;
	
	HWND hStreamWnd;
	GLuint frameTex1, frameTex2;
	GLuint frameVBuf1, frameVBuf2;
	GLuint frameIBuf1, frameIBuf2;
	GLuint frameVShader, frameFShader, frameShaderProg;
	GLint shaderTextureUniform, shaderProjMatUniform;

	bool updateTexture;
	bool updatedTexture;

	CRITICAL_SECTION updateFrameLock;
	HANDLE hNewImageEvent;
	HANDLE hCloseEvent;
	HANDLE hClosedEvent;
	
	HANDLE hTemp_WindowEvent;

	HANDLE hUpdateThread;


	int curFrameCount; clock_t lastMeasureTime;

	HINSTANCE hInstance;
public:
	CamWindow(WORD id)
	{
		this->camID = id;
		updateTexture = true;
		updatedTexture = false;
	}
	bool Init(HINSTANCE hInstance, unsigned char count, int width, int height)
	{
		if (count != 2 || width != 640 || height != 480)
			return false;
		this->hInstance = hInstance;
		hTemp_WindowEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
		CloseHandle(CreateThread(0,0, ShowCamWndThread, this, 0, NULL));
		WaitForSingleObject(hTemp_WindowEvent, INFINITE);

		CloseHandle(hTemp_WindowEvent); hTemp_WindowEvent = NULL;
		if (hStreamWnd)
		{
			bool glInited = initGL(hStreamWnd);
			if (!glInited)
				printf("[CamWindow] error : Unable to create the stream window!\n");
			else
			{
				resizeGL(2*640, 480);

				glProvokingVertex(GL_POINTS);
				glFrontFace(GL_CCW);
				glCullFace(GL_BACK);
				glEnable(GL_CULL_FACE);
				glDisable(GL_DEPTH_TEST);
				glDisable(GL_BLEND);
				glDisable(GL_LIGHTING);
			
				glEnable(GL_TEXTURE_2D);
			
				glGenTextures(1, &frameTex1);
				glGenTextures(1, &frameTex2);

				glGenBuffers(1, &frameVBuf1);
				glBindBuffer(GL_ARRAY_BUFFER, frameVBuf1);
				glBufferData(GL_ARRAY_BUFFER, sizeof(vertex2D)*4, frameVertices1, GL_STATIC_DRAW);

				glGenBuffers(1, &frameVBuf2);
				glBindBuffer(GL_ARRAY_BUFFER, frameVBuf2);
				glBufferData(GL_ARRAY_BUFFER, sizeof(vertex2D)*4, frameVertices2, GL_STATIC_DRAW);
			
				glGenBuffers(1, &frameIBuf1);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, frameIBuf1);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(BYTE)*6, frameIndices1, GL_STATIC_DRAW);
			
				glGenBuffers(1, &frameIBuf2);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, frameIBuf2);
				glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(BYTE)*6, frameIndices2, GL_STATIC_DRAW);

				glMatrixMode(GL_PROJECTION);
				glLoadIdentity();
				glViewport(0, 0, 2*640, 480);
				glOrtho(0, 2*640, 0, 480, 0, 1);

				frameVShader = glCreateShader(GL_VERTEX_SHADER);
				glShaderSource(frameVShader, 1, &faceVertexShader1, NULL);
				glCompileShader(frameVShader);
				GLint shaderCompiled = 0;
				glGetShaderiv(frameVShader, GL_COMPILE_STATUS, &shaderCompiled);
				if (shaderCompiled == GL_FALSE)
					printf("[CamWindow] error : Unable to compile the vertex shader!\n");

				frameFShader = glCreateShader(GL_FRAGMENT_SHADER);
				glShaderSource(frameFShader, 1, &faceFragmentShader1, NULL);
				glCompileShader(frameFShader);
				glGetShaderiv(frameVShader, GL_COMPILE_STATUS, &shaderCompiled);
				if (shaderCompiled == GL_FALSE)
					printf("[CamWindow] error : Unable to compile the fragment shader!\n");

				frameShaderProg = glCreateProgram();
				glAttachShader(frameShaderProg, frameVShader);
				glBindAttribLocation(frameShaderProg, 0, "_in_vec_pos");
				glBindAttribLocation(frameShaderProg, 1, "_in_tex_pos");
				glAttachShader(frameShaderProg, frameFShader);

				glLinkProgram(frameShaderProg);
			
				glGetProgramiv(frameShaderProg, GL_LINK_STATUS, &shaderCompiled);
				if (shaderCompiled == GL_FALSE)
					printf("[CamWindow] error : Unable to link the shader program!\n");

				shaderTextureUniform = glGetUniformLocation(frameShaderProg, "_in_tex");
				shaderProjMatUniform = glGetUniformLocation(frameShaderProg, "_in_ProjMat");

				ShowWindow(hStreamWnd, SW_SHOW);

				wglMakeCurrent(hDC,NULL); //OnData can be called from another thread
				
				hNewImageEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
				hCloseEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
				hClosedEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
				InitializeCriticalSection(&updateFrameLock);

				hUpdateThread = CreateThread(0,0, UpdateFrameThread, this, 0, NULL);
				return true;
			}
			CloseWindow(hStreamWnd);
		}
		return false;
	}

	void Close()
	{
		CloseWindow(hStreamWnd);
		SetEvent(hCloseEvent);
		WaitForSingleObject(hClosedEvent, INFINITE);
		//TerminateThread(hUpdateThread, 0);
		DeleteCriticalSection(&updateFrameLock);
		CloseHandle(hNewImageEvent);
		CloseHandle(hCloseEvent);
		CloseHandle(hClosedEvent);
		wglDeleteContext(hGLRC);
	}

	void OnData(const BYTE *buffer)
	{
		clock_t curClock = clock();
		curFrameCount++;
		if ((curClock - lastMeasureTime) >= 1000)
		{
			float fps = (float)curFrameCount / ((float)(curClock - lastMeasureTime) / 1000);
			char sprntTmp[64];
			sprintf_s(sprntTmp, "WMR Stream %u - FPS %f", camID, fps);
			SetWindowTextA(hStreamWnd, sprntTmp);
			//printf("FPS %f\n", fps);
			lastMeasureTime = curClock;
			curFrameCount = 0;
		}
		if (updateTexture)
		{
			EnterCriticalSection(&updateFrameLock);
			memcpy(curTextures, buffer, 2*640*480);
			//updatedTexture = true;
			LeaveCriticalSection(&updateFrameLock);
			SetEvent(hNewImageEvent);
		}
	}
private:
	static DWORD WINAPI ShowCamWndThread(PVOID _pObject)
	{
		CamWindow *pObject = (CamWindow*)_pObject;
		pObject->CreateCamWindow(pObject->hInstance, 2*640, 480);
		SetEvent(pObject->hTemp_WindowEvent);
		if (pObject->hStreamWnd == NULL) return 0;
		MSG msg; ZeroMemory(&msg, sizeof(MSG));
		while (GetMessage(&msg, pObject->hStreamWnd, 0, 0))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (!IsWindow(pObject->hStreamWnd))
				break;
		}
		//ExitProcess(0);
		return 0;
	}
	bool CreateCamWindow(HINSTANCE hInstance, int width, int height)
	{
		HWND hWnd = CreateWindowW(TEXT("WMRCAM_WND"), TEXT(""), WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS|WS_CLIPCHILDREN|WS_VISIBLE, 
			10, 10, width, height,
			NULL, NULL, hInstance, this);
		if (hWnd == NULL)
		{
			printf("[CamWindow] error : Unable to create the camera window!\n");
			return false;
		}
		UpdateWindow(hWnd);
		this->hStreamWnd = hWnd;
		return true;
	}
	
	bool initGL(HWND hWnd)
	{
		glShadeModel(GL_SMOOTH);
		glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
		glEnable(GL_TEXTURE_2D);

		RECT winRct; GetWindowRect(hWnd, &winRct);

		hDC = GetDC(hWnd);
		if (hDC == NULL)
			return false;
	
		PIXELFORMATDESCRIPTOR pxfmtdesc; ZeroMemory(&pxfmtdesc, sizeof(PIXELFORMATDESCRIPTOR));
		pxfmtdesc.nSize = sizeof(PIXELFORMATDESCRIPTOR);
		pxfmtdesc.nVersion = 1;
		pxfmtdesc.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
		pxfmtdesc.iPixelType = PFD_TYPE_RGBA;
		pxfmtdesc.cColorBits = 32;
		//pxfmtdesc.cAlphaBits = 8;
		pxfmtdesc.cDepthBits = 32;
		pxfmtdesc.cStencilBits = 8;
		pxfmtdesc.iLayerType = PFD_MAIN_PLANE;
		DWORD _pxFmt = GL_RGBA;
		if (!(_pxFmt = ChoosePixelFormat(hDC, &pxfmtdesc)))
		{
			return false;
		}
		if (!SetPixelFormat(hDC, _pxFmt, &pxfmtdesc))
		{
			return false;
		}
		HGLRC tmpHGLRC;
		if (!(tmpHGLRC = wglCreateContext(hDC)))
		{
			return false;
		}
		if (!wglMakeCurrent(hDC, tmpHGLRC))
		{
			return false;
		} 
		hGLRC = tmpHGLRC;
		GLenum err = glewInit();
		if (err != GLEW_OK)
		{
			return false;
		}
		if (!GLEW_VERSION_3_1)
			return false;
		int attribs[] = 
		{
			WGL_CONTEXT_MAJOR_VERSION_ARB, 3,
			WGL_CONTEXT_MINOR_VERSION_ARB, 1,
			WGL_CONTEXT_FLAGS_ARB, 0,
			0
		};

		hGLRC = wglCreateContextAttribsARB(hDC, tmpHGLRC, attribs);
	
		wglMakeCurrent(hDC,NULL);
		wglDeleteContext(tmpHGLRC);
		wglMakeCurrent(hDC, hGLRC);

		glClearColor (1.0, 1.0, 1.0, 0.0);
		glEnable( GL_MULTISAMPLE );
		return true;
	}

	void resizeGL(GLsizei w, GLsizei h)
	{
		if (!h) return;
		glViewport(0, 0, w, h);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		gluPerspective(90.0f,(GLfloat)w/(GLfloat)h,0.1f,10000.0f);
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();
	}

	static DWORD WINAPI UpdateFrameThread(PVOID _pObject)
	{
		CamWindow *pObject = (CamWindow*)_pObject;
		wglMakeCurrent(pObject->hDC, pObject->hGLRC);
		while (true)
		{
			HANDLE waitEventList[2] = {pObject->hNewImageEvent, pObject->hCloseEvent};
			DWORD wfoResult = WaitForMultipleObjects(2, waitEventList, FALSE, INFINITE);
			if (wfoResult == WAIT_OBJECT_0+1)
			{
				break;
			}
		
			EnterCriticalSection(&pObject->updateFrameLock);
			pObject->updatedTexture = false;
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, pObject->frameTex1);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 640, 480, 0, GL_RED, GL_UNSIGNED_BYTE, pObject->curTextures[0]);

			glActiveTexture(GL_TEXTURE1);
			glBindTexture(GL_TEXTURE_2D, pObject->frameTex2);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
			glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, 640, 480, 0, GL_RED, GL_UNSIGNED_BYTE, pObject->curTextures[1]);
			LeaveCriticalSection(&pObject->updateFrameLock);

			glMatrixMode(GL_PROJECTION);
			glLoadIdentity();
			glViewport(0, 0, 2*640, 480);
			glMatrixMode(GL_MODELVIEW);
			glLoadIdentity();

			glUseProgram(pObject->frameShaderProg);
		
			glEnableVertexAttribArray(0);
			glEnableVertexAttribArray(1);

			glm::mat4x4 projection = glm::mat4x4();
			glm::mat4x4 modelview = glm::mat4x4();
			glGetFloatv(GL_PROJECTION_MATRIX, glm::value_ptr(projection));
			glGetFloatv(GL_MODELVIEW_MATRIX, glm::value_ptr(modelview));
			glUniformMatrix4fv(pObject->shaderProjMatUniform, 1, GL_FALSE, glm::value_ptr(projection * modelview));
			//Render
			{
				glUniform1i(pObject->shaderTextureUniform, 0);

				glBindBuffer(GL_ARRAY_BUFFER, pObject->frameVBuf1);
				glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vertex2D), 0);
				glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex2D), (const GLvoid*)(2*sizeof(float)));
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pObject->frameIBuf1);
		
				glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, NULL);

				glUniform1i(pObject->shaderTextureUniform, 1);

				glBindBuffer(GL_ARRAY_BUFFER, pObject->frameVBuf2);
				glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(vertex2D), 0);
				glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(vertex2D), (const GLvoid*)(2*sizeof(float)));
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, pObject->frameIBuf2);
		
				glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_BYTE, NULL);
			}
			glDisableVertexAttribArray(0);
			glDisableVertexAttribArray(1);
			glUseProgram(0);
	
			SwapBuffers(pObject->hDC);
		
			//Sleep(10);
		}
		SetEvent(pObject->hClosedEvent);
		return 0;
	}

};

static std::vector<CamWindow*> camWindows;
static HINSTANCE g_hInstance;

bool CamWindow_Init(HINSTANCE hInstance)
{
	g_hInstance = hInstance;

	WNDCLASS wc; ZeroMemory(&wc, sizeof(WNDCLASS));
	wc.hbrBackground = (HBRUSH)GetStockObject(LTGRAY_BRUSH);
	wc.style = CS_OWNDC | CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;
	wc.lpfnWndProc = (WNDPROC)CameraWindowProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = TEXT("WMRCAM_WND");
	if (!RegisterClass(&wc))
	{
		MessageBox(NULL, TEXT("Unable to register the window class!"), TEXT("ERROR"), 16);
		return false;
	}
	return true;
}

CamWindow *CamWindow_Add(unsigned short camID, unsigned char count, unsigned int sizeX, unsigned int sizeY)
{
	if (count != 2 || sizeX != 640 || sizeY != 480)
		return nullptr;

	for (size_t i = 0; i < camWindows.size(); i++)
	{
		if (camWindows[i]->camID == camID)
			return camWindows[i];
	}

	CamWindow *ret = new CamWindow(camID);
	if (!ret->Init(g_hInstance, 2, 640, 480))
	{
		delete ret;
		return nullptr;
	}
	camWindows.push_back(ret);
	return ret;
}

void CamWindow_OnData(unsigned short camID, unsigned char count, unsigned int sizeX, unsigned int sizeY, const BYTE *buffer)
{
	CamWindow *pWindow = CamWindow_Add(camID, count, sizeX, sizeY);
	if (pWindow != nullptr)
	{
		pWindow->OnData(buffer);
	}
	else
		printf("Can't open a window for stream %u : %u times %ux%u.\n", (unsigned int)camID, (unsigned int)count, sizeX, sizeY);
}

void CamWindow_OnCloseCam(unsigned short camID)
{
	for (size_t i = 0; i < camWindows.size(); i++)
	{
		if (camWindows[i]->camID == camID)
		{
			camWindows[i]->Close();
			delete camWindows[i];
			camWindows.erase(camWindows.begin() + i);
			i--;
		}
	}
}

void CamWindow_Close()
{
	for (size_t i = 0; i < camWindows.size(); i++)
	{
		camWindows[i]->Close();
		delete camWindows[i];
	}
	camWindows.clear();
	UnregisterClass(TEXT("WMRCAM_WND"), g_hInstance);
}

INT_PTR CALLBACK CameraWindowProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;
	}
	return DefWindowProc(hDlg, message, wParam, lParam);
}