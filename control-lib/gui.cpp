#include "precompiled.h"

#pragma warning( push, 0 )

#include <imgui.h>
#include <imgui_impl_opengl2.h>
#include <imgui_impl_glfw.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <GLFW/glfw3.h>

#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>

#pragma warning( pop )

#include "gui.h"
#include "list.h"
#include "connection.h"
#include "plugins/plugin.h"

#pragma comment(lib, "OpenGL32.lib")
#pragma comment(lib, "GLFW.lib")

//const char PLUGIN_NAME_GUI[] = "Gui";

struct connectToData_t {
	HWND label;
	HWND editAddr;
	HWND editPort;
	HWND button;
};

struct gui_type {
	void						( *onDataSourceCallback )( void * const, struct dataSourceInterface_type * const );
	void *						onDataSourceParam;
	ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);
	GLFWwindow* window;
};

/*
========================
allocate
========================
*/
static void* allocate( const size_t size ) {
	return malloc( size );
}

/*
========================
deallocate
========================
*/
static void deallocate( void * const ptr ) {
	free( ptr );
}

static void __declspec(nothrow) glfw_error_callback(int error, const char* description)
{
	fprintf(stderr, "GLFW Error %d: %s\n", error, description);
}

/*
========================
GUI_Create
========================
*/
struct gui_type* GUI_Create(void (*cb)(void* const, struct dataSourceInterface_type* const), void* const param) {
	glfwSetErrorCallback(glfw_error_callback);
	if (!glfwInit())
		return nullptr;

	// Create window with graphics context
	GLFWwindow* window = glfwCreateWindow(1280, 720, "Dear ImGui GLFW+OpenGL2 example", nullptr, nullptr);
	if (window == nullptr)
		return nullptr;

	struct gui_type* const me = (struct gui_type*)allocate(sizeof(struct gui_type));

	me->onDataSourceCallback = cb;
	me->onDataSourceParam = param;

	me->window = window;

	glfwMakeContextCurrent(window);
	glfwSwapInterval(1); // Enable vsync

	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();
	//ImGui::StyleColorsLight();

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL2_Init();

	// Load Fonts
	// - If no fonts are loaded, dear imgui will use the default font. You can also load multiple fonts and use ImGui::PushFont()/PopFont() to select them.
	// - AddFontFromFileTTF() will return the ImFont* so you can store it if you need to select the font among multiple.
	// - If the file cannot be loaded, the function will return a nullptr. Please handle those errors in your application (e.g. use an assertion, or display an error and quit).
	// - The fonts will be rasterized at a given size (w/ oversampling) and stored into a texture when calling ImFontAtlas::Build()/GetTexDataAsXXXX(), which ImGui_ImplXXXX_NewFrame below will call.
	// - Use '#define IMGUI_ENABLE_FREETYPE' in your imconfig file to use Freetype for higher quality font rendering.
	// - Read 'docs/FONTS.md' for more instructions and details.
	// - Remember that in C/C++ if you want to include a backslash \ in a string literal you need to write a double backslash \\ !
	//io.Fonts->AddFontDefault();
	//io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\segoeui.ttf", 18.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/DroidSans.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Roboto-Medium.ttf", 16.0f);
	//io.Fonts->AddFontFromFileTTF("../../misc/fonts/Cousine-Regular.ttf", 15.0f);
	//ImFont* font = io.Fonts->AddFontFromFileTTF("c:\\Windows\\Fonts\\ArialUni.ttf", 18.0f, nullptr, io.Fonts->GetGlyphRangesJapanese());
	//IM_ASSERT(font != nullptr);

	return me;
}

/*
========================
GUI_Destroy
========================
*/
void GUI_Destroy( struct gui_type * const me ) {
	ImGui_ImplOpenGL2_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();

	glfwDestroyWindow(me->window);
	glfwTerminate();

	deallocate( me );
}

/*
========================
GUI_Render
========================
*/
int32_t GUI_Begin( struct gui_type * const me ) {
	if (glfwWindowShouldClose(me->window))
		return 0;

	// Poll and handle events (inputs, window resize, etc.)
	// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
	// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
	// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
	// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
	glfwPollEvents();
//	if (glfwGetWindowAttrib(me->window, GLFW_ICONIFIED) != 0)
//	{
//	}

	// Start the Dear ImGui frame
	ImGui_ImplOpenGL2_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	return 1;
}

void GUI_End( struct gui_type * const me ) {
	// Rendering
	ImGui::Render();
	int display_w, display_h;
	glfwGetFramebufferSize(me->window, &display_w, &display_h);
	glViewport(0, 0, display_w, display_h);
	glClearColor(me->clear_color.x * me->clear_color.w, me->clear_color.y * me->clear_color.w, me->clear_color.z * me->clear_color.w, me->clear_color.w);
	glClear(GL_COLOR_BUFFER_BIT);

	// If you are using this code with non-legacy OpenGL header/contexts (which you should not, prefer using imgui_impl_opengl3.cpp!!),
	// you may need to backup/reset/restore other state, e.g. for current shader using the commented lines below.
	//GLint last_program;
	//glGetIntegerv(GL_CURRENT_PROGRAM, &last_program);
	//glUseProgram(0);
	ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
	//glUseProgram(last_program);

	glfwMakeContextCurrent(me->window);
	glfwSwapBuffers(me->window);
}
