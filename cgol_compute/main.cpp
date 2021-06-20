/*
 * Copyright 2021 Simon Thalén. All rights reserved.
 */

#include <cmath>
#include <stdio.h>
#include <bx/bx.h>
#include <bx/spscqueue.h>
#include <bx/thread.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/ext.hpp> 
#include <iostream>

#if BX_PLATFORM_LINUX
#define GLFW_EXPOSE_NATIVE_X11
#elif BX_PLATFORM_WINDOWS
#define GLFW_EXPOSE_NATIVE_WIN32
#elif BX_PLATFORM_OSX
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <GLFW/glfw3native.h>


#include <fstream>
using namespace std;
using namespace glm;

static bx::DefaultAllocator s_allocator;
static bx::SpScUnboundedQueue s_apiThreadEvents(&s_allocator);

bool show_cursor = true,wireframe=false;
GLFWwindow *window;

//For drawing the render texture on a quad
static float quad[] =
		   {-1.0f, -1.0f, 0.0f,  0.0f,  0.0f, 1.0f,
			 1.0f,  1.0f, 0.0f,  1.0f,  1.0f, 1.0f,
			
			-1.0f,  1.0f, 0.0f,  0.0f,  1.0f, 1.0f,
			 
			
			 1.0f,  1.0f, 0.0f,  1.0f,  1.0f, 1.0f,
			-1.0f, -1.0f, 0.0f,  0.0f,  0.0f, 1.0f,
			 1.0f, -1.0f, 0.0f,  1.0f, 0.0f, 1.0f};

//Load a shader
bgfx::ShaderHandle loadShader(const char* _name) {
	char* data;
    size_t size;
    ifstream file (_name, ios::in|ios::binary|ios::ate);
	
    if(file.is_open()) {
		size = file.tellg();
        //file.seekg(0, ios::end);
        data = new char[size];
		file.seekg(0, ios::beg);
        file.read(data, size);
		file.close();
    }
	const bgfx::Memory* mem = bgfx::copy(data,size+1);
	mem->data[mem->size-1] = '\0';
	bgfx::ShaderHandle handle = bgfx::createShader(mem);
	bgfx::setName(handle, _name);
	delete[] data;
    return handle;
}


//Events class and structs

enum class EventType
{
	Exit,
	Key,
	Resize,
	Scroll
};

struct ExitEvent
{
	EventType type = EventType::Exit;
};

struct KeyEvent
{
	EventType type = EventType::Key;
	int key;
	int action;
};
struct ScrollEvent
{
	EventType type = EventType::Scroll;
	double x;
};

struct ResizeEvent
{
	EventType type = EventType::Resize;
	uint32_t width;
	uint32_t height;
};


static void glfw_errorCallback(int error, const char *description)
{
	fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

static void glfw_keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
	auto keyEvent = new KeyEvent;
	keyEvent->key = key;
	keyEvent->action = action;
	s_apiThreadEvents.push(keyEvent);
}

void glfw_scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
	auto scrollEvent = new ScrollEvent;
	scrollEvent->x = xoffset;
	s_apiThreadEvents.push(scrollEvent);
}

struct ApiThreadArgs
{
	bgfx::PlatformData platformData;
	uint32_t width;
	uint32_t height;
};

static int32_t runApiThread(bx::Thread *self, void *userData)
{
	//Size of the conways game of life "board"
	int size=1024;

	double begintime = glfwGetTime();
	auto args = (ApiThreadArgs *)userData;
	bgfx::Init init;
	
	init.type = bgfx::RendererType::Metal;
	init.debug = true;
	init.platformData = args->platformData;
	init.resolution.width = args->width;
	init.resolution.height = args->height;
	init.resolution.reset = BGFX_RESET_VSYNC ;
	if (!bgfx::init(init))
		return 1;

	const bgfx::ViewId kClearView = 0;
	bgfx::setViewClear(0
			, BGFX_CLEAR_COLOR|BGFX_CLEAR_DEPTH
			, 0x443355FF
			, 1.0f
			, 0
			);
	bgfx::VertexLayout layout;
	layout.begin()
			.add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
			.add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
			.add(bgfx::Attrib::Color0, 1, bgfx::AttribType::Float)
			.end();

	bgfx::ProgramHandle m_program = bgfx::createProgram(loadShader("v_simple.bin"),loadShader("f_simple.bin"),  true);
	bgfx::ProgramHandle compute_shader = bgfx::createProgram(loadShader("compute.bin"), true);
	bgfx::ProgramHandle start_shader = bgfx::createProgram(loadShader("start.bin"), true);

	bgfx::TextureHandle A_texture = bgfx::createTexture2D(
		size,
		size,
		false,
		1,
		bgfx::TextureFormat::RGBA32F,
		BGFX_TEXTURE_COMPUTE_WRITE|BGFX_SAMPLER_POINT|BGFX_TEXTURE_BLIT_DST);

	bgfx::TextureHandle B_texture = bgfx::createTexture2D(
		size,
		size,
		false,
		1,
		bgfx::TextureFormat::RGBA32F,
		BGFX_TEXTURE_COMPUTE_WRITE|BGFX_SAMPLER_POINT|BGFX_TEXTURE_BLIT_DST);

	bgfx::UniformHandle texture_uniform = bgfx::createUniform("s_tex", bgfx::UniformType::Sampler);
	bgfx::UniformHandle in_tex_uniform = bgfx::createUniform("in_tex",bgfx::UniformType::Sampler); 
	bgfx::VertexBufferHandle quad_buffer  = bgfx::createVertexBuffer(bgfx::copy(quad,6*6*sizeof(float)),layout);
	
	bgfx::setDebug(BGFX_DEBUG_TEXT);
	
	uint32_t width = args->width;
	uint32_t height = args->height;
	bool showStats = false;
	bool exit = false;
	ivec2 pos = ivec2(0,0);
	double scroll=0;

	glfwSetInputMode(window,GLFW_CURSOR,GLFW_CURSOR_NORMAL);	
	
	bgfx::setImage(1,A_texture,0,bgfx::Access::Write);
	bgfx::dispatch(1,start_shader,size/4,size/4);
	bgfx::blit(1, B_texture, 0, 0, A_texture, 0, 0, width, height);
	
	bool flip = false;
	while (!exit)
	{
		while (auto ev = (EventType *)s_apiThreadEvents.pop()) {
			if (*ev == EventType::Key) {
				auto keyEvent = (KeyEvent *)ev;

				if (keyEvent->key == GLFW_KEY_F1 && keyEvent->action == GLFW_RELEASE)
					showStats = !showStats;
				if (keyEvent->key == GLFW_KEY_UP && keyEvent->action == GLFW_RELEASE)
					{
						if(scroll + 0.1 <= 0)
							scroll = scroll + 0.1;
					}
				if (keyEvent->key == GLFW_KEY_DOWN && keyEvent->action == GLFW_RELEASE)
					{
						if(scroll - 0.1 >= -1.8)
							scroll =scroll - 0.1;
					}

				if (keyEvent->key == GLFW_KEY_ESCAPE && keyEvent->action == GLFW_RELEASE)
					{
						exit = true;
					}
			}
			else if (*ev == EventType::Scroll) {
				//printf("GLFW scroll");
				//auto scrollEvent = (ScrollEvent *)ev;
				//	scroll = scroll+scrollEvent->x;
			}

			else if (*ev == EventType::Resize) {
				auto resizeEvent = (ResizeEvent *)ev;
				bgfx::reset(resizeEvent->width, resizeEvent->height, BGFX_RESET_VSYNC | BGFX_RESET_MSAA_X16 |BGFX_RESET_HDR10);
				bgfx::setViewRect(kClearView, 0, 0, bgfx::BackbufferRatio::Equal);
				width = resizeEvent->width;
				height = resizeEvent->height;
			} else if (*ev == EventType::Exit) {
				exit = true;
			}
			delete ev;
		}

		//bgfx::touch(0);

		//::dbgTextClear();
		//bgfx::dbgTextPrintf(0, 0, 0x0f, "%f", scroll);

		//mat4 Orthographic = glm::ortho(0, 512,0, 512,0, 100);
		mat4 Projection = glm::perspective(glm::pi<float>() * 0.25f, (float)height/float(width), 0.01f, 1000.f);
		mat4 View = lookAt(vec3(0,0,2+scroll),vec3(0,0,0),vec3(0,1,0));
		
		bgfx::setViewTransform(0, value_ptr(View), value_ptr(Projection));
		bgfx::setViewRect(0, 0, 0, uint16_t(width), uint16_t(height) );
		
		mat4 Model = mat4(1.0f);
		bgfx::setTransform(value_ptr(Model));

		if(flip)
		{
		 bgfx::setImage(1,B_texture,0,bgfx::Access::Write);
		bgfx::setTexture(0,in_tex_uniform,A_texture);
	    bgfx::dispatch(0,compute_shader,size/4,size/4);
		bgfx::setTexture(0,texture_uniform,B_texture);
		}
		else
		{

		bgfx::setImage(1,A_texture,0,bgfx::Access::Write);
		bgfx::setTexture(0,in_tex_uniform,B_texture);
	    bgfx::dispatch(0,compute_shader,size/4,size/4);
		bgfx::setTexture(0,texture_uniform,A_texture);
		}
		bgfx::setVertexBuffer(0, quad_buffer, 0, 8);
		
		bgfx::setState(BGFX_STATE_DEFAULT);
		bgfx::submit(0,m_program);
		bgfx::frame(false);
		flip = !flip;
		//bgfx::blit(1, A_texture, 0, 0, B_texture, 0, 0, width, height);
	
	}
	bgfx::shutdown();
	
	return 0;
}

int main(int argc, char **argv)
{

	// Create a GLFW window without an OpenGL context.
	glfwSetErrorCallback(glfw_errorCallback);
	if (!glfwInit())
		return 1;
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	window = glfwCreateWindow(1024,	1024, "Conways Game Of Life using Compute shaders",  nullptr, nullptr);
	if (!window)
		return 1;
	//Set the callbacks for keys and mousescroll
	glfwSetKeyCallback(window, glfw_keyCallback);
	glfwSetScrollCallback(window, glfw_scroll_callback);
	// Call bgfx::renderFrame before bgfx::init to signal to bgfx not to create a render thread.
	// Most graphics APIs must be used on the same thread that created the window.
	bgfx::renderFrame();
	// Create a thread to call the bgfx API from (except bgfx::renderFrame).
	ApiThreadArgs apiThreadArgs;
#if BX_PLATFORM_LINUX || BX_PLATFORM_BSD
	apiThreadArgs.platformData.ndt = glfwGetX11Display();
	apiThreadArgs.platformData.nwh = (void*)(uintptr_t)glfwGetX11Window(window);
#elif BX_PLATFORM_OSX
	apiThreadArgs.platformData.nwh = glfwGetCocoaWindow(window);
#elif BX_PLATFORM_WINDOWS
	apiThreadArgs.platformData.nwh = glfwGetWin32Window(window);
#endif
	int width, height;
	glfwGetWindowSize(window, &width, &height);
	apiThreadArgs.width = (uint32_t)width;
	apiThreadArgs.height = (uint32_t)height;
	bx::Thread apiThread;
	apiThread.init(runApiThread, &apiThreadArgs);
	// Run GLFW message pump.
	bool exit = false;
	while (!exit) {
		glfwPollEvents();
		// Send window close event to the API thread.
		if (glfwWindowShouldClose(window)) {
			s_apiThreadEvents.push(new ExitEvent);
			exit = true;
		}
		// Send window resize event to the API thread.
		int oldWidth = width, oldHeight = height;
		glfwGetWindowSize(window, &width, &height);
		if (width != oldWidth || height != oldHeight) {
			auto resize = new ResizeEvent;
			resize->width = (uint32_t)width;
			resize->height = (uint32_t)height;
			s_apiThreadEvents.push(resize);
		}
		// Wait for the API thread to call bgfx::frame, then process submitted rendering primitives.
		bgfx::renderFrame();
	}
	// Wait for the API thread to finish before shutting down.
	while (bgfx::RenderFrame::NoContext != bgfx::renderFrame()) {}
	apiThread.shutdown();
	glfwTerminate();
	return apiThread.getExitCode();
}
