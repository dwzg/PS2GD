// Pulse Dash headless test harness built on the Play! PS2 emulator core
// (https://github.com/jpd002/Play-, BSD licensed). Built by scripts/emu-test.sh,
// which drops this file in place of Play!'s tools/AutoTest/Main.cpp.
//
// Usage: autotest <game.elf> <seconds> [script]
//   script: comma separated "frame:BUTTON:frames" presses, e.g. "300:CROSS:5,420:CROSS:5"
//   PD_SHOTS=150,300  also render through the OpenGL GS (Mesa, headless EGL)
//                     and save shot_<frame>.ppm in the working directory.
// The game's printf output (boot log, periodic status lines) goes to stdout.
#include "PS2VM.h"
#include "PS2VM_Preferences.h"
#include "DefaultAppConfig.h"
#include "StdStream.h"
#include "iop/IopBios.h"
#include "gs/GSH_Null.h"
#include "gs/GSH_OpenGL/GSH_OpenGL.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include "PH_Generic.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <thread>
#include <vector>

struct Press
{
	unsigned frame;
	PS2::CControllerInfo::BUTTON button;
	unsigned length;
};

static PS2::CControllerInfo::BUTTON ParseButton(const std::string& n)
{
	static const std::map<std::string, PS2::CControllerInfo::BUTTON> m = {
	    {"CROSS", PS2::CControllerInfo::CROSS}, {"CIRCLE", PS2::CControllerInfo::CIRCLE},
	    {"SQUARE", PS2::CControllerInfo::SQUARE}, {"TRIANGLE", PS2::CControllerInfo::TRIANGLE},
	    {"START", PS2::CControllerInfo::START}, {"UP", PS2::CControllerInfo::DPAD_UP},
	    {"DOWN", PS2::CControllerInfo::DPAD_DOWN}, {"LEFT", PS2::CControllerInfo::DPAD_LEFT},
	    {"RIGHT", PS2::CControllerInfo::DPAD_RIGHT}};
	auto it = m.find(n);
	if(it == m.end()) throw std::runtime_error("bad button " + n);
	return it->second;
}


// OpenGL GS renderer on a headless Mesa EGL context (no window).
class CGSH_OpenGLHeadless : public CGSH_OpenGL
{
public:
	static FactoryFunction GetFactoryFunction()
	{
		return []() { return new CGSH_OpenGLHeadless(); };
	}

protected:
	void InitializeImpl() override
	{
		auto getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
		m_dpy = getPlatformDisplay ? getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr)
		                           : eglGetDisplay(EGL_DEFAULT_DISPLAY);
		EGLint major, minor;
		if(!eglInitialize(m_dpy, &major, &minor)) throw std::runtime_error("eglInitialize failed");
		eglBindAPI(EGL_OPENGL_API);
		EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
		                    EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
		EGLConfig cfg;
		EGLint n = 0;
		eglChooseConfig(m_dpy, cfgAttr, &cfg, 1, &n);
		EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
		                    EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
		m_ctx = eglCreateContext(m_dpy, n ? cfg : nullptr, EGL_NO_CONTEXT, ctxAttr);
		if(m_ctx == EGL_NO_CONTEXT) throw std::runtime_error("eglCreateContext failed");
		if(n)
		{
			EGLint pbAttr[] = {EGL_WIDTH, 64, EGL_HEIGHT, 64, EGL_NONE};
			m_surf = eglCreatePbufferSurface(m_dpy, cfg, pbAttr);
		}
		eglMakeCurrent(m_dpy, m_surf, m_surf, m_ctx);
		glewExperimental = GL_TRUE;
		glewInit(); // the GLX part fails without an X display; GL entry points are loaded anyway
		printf("harness: GL %s / %s\n", (const char*)glGetString(GL_VERSION), (const char*)glGetString(GL_RENDERER));
		CGSH_OpenGL::InitializeImpl();
	}
	void PresentBackbuffer() override
	{
	}

private:
	EGLDisplay m_dpy = EGL_NO_DISPLAY;
	EGLContext m_ctx = EGL_NO_CONTEXT;
	EGLSurface m_surf = EGL_NO_SURFACE;
};

static void SaveShot(CPS2VM& vm, const std::string& path)
{
	auto gs = dynamic_cast<CGSH_OpenGL*>(vm.GetGSHandler());
	if(!gs) return;
	auto info = gs->GetCurrentDisplayInfo();
	for(int layerIdx = 1; layerIdx >= 0; layerIdx--)
	{
		auto& layer = info.layers[layerIdx];
		if(!layer.enabled) continue;
		uint64 frame = (uint64)(layer.bufPtr / 8192) | ((uint64)(layer.bufWidth / 64) << 16) | ((uint64)layer.psm << 24);
		auto bmp = gs->GetFramebuffer(frame);
		if(bmp.IsEmpty()) continue;
		FILE* f = fopen(path.c_str(), "wb");
		unsigned w = bmp.GetWidth(), h = bmp.GetHeight();
		fprintf(f, "P6\n%u %u\n255\n", w, h);
		auto px = reinterpret_cast<const uint8*>(bmp.GetPixels());
		for(unsigned y = 0; y < h; y++)
			for(unsigned x = 0; x < w; x++)
			{
				const uint8* p = px + (y * w + x) * 4; // BGRA
				uint8 rgb[3] = {p[2], p[1], p[0]};
				fwrite(rgb, 1, 3, f);
			}
		fclose(f);
		printf("harness: saved %s (%ux%u, layer %d)\n", path.c_str(), w, h, layerIdx);
		return;
	}
	printf("harness: no framebuffer for screenshot\n");
}

int main(int argc, const char** argv)
{
	if(argc < 3)
	{
		printf("usage: %s game.elf seconds [script]\n", argv[0]);
		return 2;
	}
	std::vector<Press> presses;
	if(argc > 3)
	{
		std::stringstream ss(argv[3]);
		std::string item;
		while(std::getline(ss, item, ','))
		{
			unsigned f, l;
			char name[32];
			if(sscanf(item.c_str(), "%u:%31[A-Z]:%u", &f, name, &l) == 3)
				presses.push_back({f, ParseButton(name), l});
		}
	}
	double seconds = atof(argv[2]);

	CPS2VM vm;
	vm.Initialize();
	const char* shotSpec = getenv("PD_SHOTS"); // e.g. "300,900" frames to capture
	std::vector<unsigned> shots;
	if(shotSpec)
	{
		std::stringstream ss(shotSpec);
		std::string item;
		while(std::getline(ss, item, ',')) shots.push_back((unsigned)atoi(item.c_str()));
	}
	if(shots.empty()) vm.CreateGSHandler(CGSH_Null::GetFactoryFunction());
	else vm.CreateGSHandler(CGSH_OpenGLHeadless::GetFactoryFunction());
	vm.CreatePadHandler(CPH_Generic::GetFactoryFunction());
	auto pad = static_cast<CPH_Generic*>(vm.GetPadHandler());

	std::atomic<unsigned> frames(0);
	auto conn = vm.OnNewFrame.Connect([&]() { frames++; });

	vm.m_ee->m_os->BootFromFile(argv[1]);
	{
		auto iopOs = dynamic_cast<CIopBios*>(vm.m_iop->m_bios.get());
		iopOs->GetIoman()->SetFileStream(Iop::CIoman::FID_STDOUT, new Framework::CStdStream(stdout));
	}
	vm.Resume();

	auto start = std::chrono::steady_clock::now();
	unsigned target = (unsigned)(seconds * 60.0);
	while(frames < target)
	{
		unsigned f = frames;
		for(auto it = shots.begin(); it != shots.end();)
		{
			if(f >= *it)
			{
				SaveShot(vm, "shot_" + std::to_string(*it) + ".ppm");
				it = shots.erase(it);
			}
			else ++it;
		}
		std::map<int, bool> state;
		for(const auto& p : presses)
			state[p.button] = state[p.button] || (f >= p.frame && f < p.frame + p.length);
		for(const auto& s : state)
			pad->SetButtonState(s.first, s.second);
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
		double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		if(wall > seconds * 20 + 60)
		{
			printf("harness: timeout (frames=%u)\n", (unsigned)frames);
			break;
		}
	}
	vm.Pause();
	double wall = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	printf("harness: %u frames in %.1fs wall\n", (unsigned)frames, wall);
	fflush(stdout);
	vm.DestroyGSHandler();
	vm.Destroy();
	return 0;
}
