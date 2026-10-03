// Pulse Dash test hooks for PPSSPP's headless runner
// (https://github.com/hrydgard/ppsspp, headless/Headless.cpp).
//
// scripts/psp-emu-test.sh copies this file into a PPSSPP checkout and makes
// Headless.cpp call PdHarnessFrame() after each emulated frame and
// PdHarnessOutput() with everything the game prints. Settings come from the
// environment:
//   PD_SCRIPT="300:CROSS:5,420:CROSS:5"  presses: frame:BUTTON:frames. "+frame:..."
//                                        counts from the frame where the game first
//                                        printed a line starting with PD_MARK (perf
//                                        builds do at each level attempt), so a level
//                                        can be played from `pd_tool script`
//   PD_SHOTS=150,300                     save shot_<frame>.ppm (the 480x272 screen)
//   PD_FRAMES=1200                       stop after this many frames
//   PD_QUIT=900                          choose HOME > Quit at this frame (runs the
//                                        game's exit callback)
// Frames are counted at the PSP's 59.94 Hz from the emulated clock.
//
// This file is part of Pulse Dash (MIT license); built into PPSSPP it
// becomes part of a GPL program, which is only used for testing.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "Core/Core.h"
#include "Core/CoreTiming.h"
#include "Core/HLE/sceCtrl.h"
#include "Core/HLE/sceDisplay.h"
#include "Core/HLE/sceKernelThread.h"
#include "Core/MemMap.h"
#include "GPU/ge_constants.h"

namespace {

struct Press {
	unsigned frame, length;
	u32 button;
	bool relative; // counted from the PD_MARK frame
};

struct Harness {
	bool init = false;
	std::vector<Press> presses;
	std::vector<unsigned> shots;
	unsigned maxFrames = 0, quitFrame = 0;
	bool quit = false;
	int mark = -1;
	unsigned frame = 0, nextFrame = 0; // the current frame, the first one not reached yet
	std::string line;
};

Harness h;

u32 ButtonBit(const std::string &name) {
	static const struct {
		const char *name;
		u32 bit;
	} names[] = {
		{"CROSS", CTRL_CROSS}, {"CIRCLE", CTRL_CIRCLE}, {"SQUARE", CTRL_SQUARE}, {"TRIANGLE", CTRL_TRIANGLE},
		{"UP", CTRL_UP}, {"DOWN", CTRL_DOWN}, {"LEFT", CTRL_LEFT}, {"RIGHT", CTRL_RIGHT},
		{"L", CTRL_LTRIGGER}, {"R", CTRL_RTRIGGER}, {"START", CTRL_START}, {"SELECT", CTRL_SELECT},
		{"HOME", CTRL_HOME},
	};
	for (const auto &n : names)
		if (name == n.name) return n.bit;
	fprintf(stderr, "harness: unknown button %s\n", name.c_str());
	return 0;
}

void Init() {
	h.init = true;
	if (const char *s = getenv("PD_SCRIPT")) {
		std::string all(s);
		size_t pos = 0;
		while (pos < all.size()) {
			size_t end = all.find(',', pos);
			if (end == std::string::npos) end = all.size();
			std::string item = all.substr(pos, end - pos);
			pos = end + 1;
			Press p{};
			p.relative = !item.empty() && item[0] == '+';
			char button[32] = {};
			if (sscanf(item.c_str() + (p.relative ? 1 : 0), "%u:%31[A-Z]:%u", &p.frame, button, &p.length) == 3) {
				p.button = ButtonBit(button);
				h.presses.push_back(p);
			}
		}
	}
	if (const char *s = getenv("PD_SHOTS")) {
		for (const char *p = s; *p;) {
			h.shots.push_back((unsigned)strtoul(p, (char **)&p, 10));
			while (*p == ',') p++;
		}
	}
	if (const char *s = getenv("PD_FRAMES")) h.maxFrames = (unsigned)strtoul(s, nullptr, 10);
	if (const char *s = getenv("PD_QUIT")) h.quitFrame = (unsigned)strtoul(s, nullptr, 10);
	printf("harness: %d presses, %d shots, %u frames\n", (int)h.presses.size(), (int)h.shots.size(), h.maxFrames);
}

// The screen as the PSP shows it, from emulated VRAM (the software renderer
// draws there).
void SaveShot(unsigned frame) {
	PSPPointer<u8> top;
	u32 stride = 0;
	GEBufferFormat fmt = GE_FORMAT_8888;
	__DisplayGetFramebuf(&top, &stride, &fmt, 0);
	const u8 *px = Memory::IsValidRange(top.ptr, stride * 272 * 4) ? Memory::GetPointerUnchecked(top.ptr) : nullptr;
	if (!px || fmt != GE_FORMAT_8888) {
		printf("harness: no 32-bit frame buffer at frame %u\n", frame);
		return;
	}
	char name[64];
	snprintf(name, sizeof(name), "shot_%u.ppm", frame);
	FILE *f = fopen(name, "wb");
	if (!f) return;
	fprintf(f, "P6\n480 272\n255\n");
	for (int y = 0; y < 272; y++)
		for (int x = 0; x < 480; x++) fwrite(px + (y * stride + x) * 4, 1, 3, f); // R, G, B (A last)
	fclose(f);
	printf("harness: saved %s\n", name);
}

} // namespace

void PdHarnessOutput(std::string_view text) {
	if (!h.init) Init();
	h.line.append(text.data(), text.size());
	size_t nl;
	while ((nl = h.line.find('\n')) != std::string::npos) {
		if (h.line.compare(0, 7, "PD_MARK") == 0 && h.mark < 0) {
			h.mark = (int)h.frame;
			printf("harness: PD_MARK at frame %d\n", h.mark);
		}
		h.line.erase(0, nl + 1);
	}
}

void PdHarnessFrame() {
	if (!h.init) Init();
	unsigned frame = (unsigned)(CoreTiming::GetGlobalTimeUs() * 60 / 1001000);
	if (frame < h.nextFrame) return;
	for (unsigned s : h.shots)
		if (s >= h.nextFrame && s <= frame) SaveShot(s);
	h.frame = frame;
	h.nextFrame = frame + 1;

	u32 held = 0;
	for (const Press &p : h.presses) {
		if (p.relative && h.mark < 0) continue;
		unsigned start = p.frame + (p.relative ? (unsigned)h.mark : 0);
		if (frame >= start && frame < start + p.length) held |= p.button;
	}
	__CtrlUpdateButtons(held, CTRL_MASK_USER & ~held);

	if (h.quitFrame && frame >= h.quitFrame && !h.quit) {
		h.quit = true;
		printf("harness: HOME > Quit at frame %u (exit callback %s)\n", frame,
		       __KernelInvokeRegisteredExitCallback() ? "called" : "not registered");
	}
	if (h.maxFrames && frame >= h.maxFrames) {
		printf("harness: stopping at frame %u\n", frame);
		Core_Stop();
	}
}
