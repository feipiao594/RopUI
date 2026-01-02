#include <glad/egl.h>
#include <glad/gles2.h>
#include <cstdint>
#include <cstdio>
#include <memory>
#include "egl_backend.h"
#include <log.hpp>
#include <platform/linux/schedule/epoll_backend.h>
#include <platform/schedule/hive.h>
#include <platform/schedule/io_worker.h>
#include <platform/schedule/worker_watcher.h>

#define NANOVG_GLES2_IMPLEMENTATION
#include <nanovg.h>
#include <nanovg_gl.h>

using namespace RopHive;
using namespace RopHive::Linux;

static void render(NVGcontext* vg, float w, float h, bool is_red) {
    nvgBeginFrame(vg, w, h, 1.0f);
    // Background
    nvgBeginPath(vg);
    nvgRect(vg, 0, 0, w, h);
    nvgFillColor(vg, nvgRGBA(20, 22, 26, 255));
    nvgFill(vg);

    // Button Geometry
    float bw = 180, bh = 60;
    float bx = w * 0.5f - bw * 0.5f;
    float by = h * 0.5f - bh * 0.5f;

    NVGcolor colA = is_red ? nvgRGBA(220, 50, 50, 255) : nvgRGBA(50, 100, 220, 255);
    NVGcolor colB = is_red ? nvgRGBA(120, 20, 20, 255) : nvgRGBA(20, 50, 150, 255);
    NVGpaint grad = nvgLinearGradient(vg, bx, by, bx, by + bh, colA, colB);

    nvgBeginPath(vg);
    nvgRoundedRect(vg, bx, by, bw, bh, 10);
    nvgFillPaint(vg, grad);
    nvgFill(vg);

    // Text
    nvgFontSize(vg, 24);
    nvgFontFace(vg, "sans");
    nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
    nvgFillColor(vg, nvgRGBA(255, 255, 255, 255));
    nvgText(vg, bx + bw * 0.5f, by + bh * 0.5f, is_red ? "STOP" : "GO", NULL);
    nvgEndFrame(vg);
}

static long long test_a = 1;
static long long test_b = 1;
static long long test_c = 1;

class WaylandFrameWatcher final : public IWorkerWatcher {
public:
    WaylandFrameWatcher(IOWorker& worker, WLContext& wl, EGLContextWl& egl, NVGcontext* vg)
        : IWorkerWatcher(worker), wl_(wl), egl_(egl), vg_(vg) {}

    void start() override {
        int fd = wl_display_get_fd(wl_.display);
        source_ = std::make_unique<PollReadinessEventSource>(fd, POLLIN, [this](uint32_t) {
          LOG(INFO)("event get! %d", test_a++); 
          this->handleEvents();
        });
        attachSource(source_.get());
        draw(); 
        prepareRead();
    }

    void stop() override {
        if (source_) {
            detachSource(source_.get());
            source_.reset();
        }
    }

private:
    void handleEvents() {
        if (wl_display_read_events(wl_.display) == -1) {
            wl_display_cancel_read(wl_.display);
        } else {
            wl_display_dispatch_pending(wl_.display);
            if (wl_.button_clicked) {
                wl_.button_clicked = false;
                checkHit();
            }
        }
        prepareRead();
    }

    void checkHit() {
        float bw = 180, bh = 60;
        float bx = wl_.width * 0.5f - bw * 0.5f;
        float by = wl_.height * 0.5f - bh * 0.5f;

        if (wl_.mouse_x >= bx && wl_.mouse_x <= bx + bw &&
            wl_.mouse_y >= by && wl_.mouse_y <= by + bh) {
            is_red_ = !is_red_;
            LOG(INFO)("click! %d", test_c++); 
            draw();
        }
    }

    void draw() {
        LOG(INFO)("draw! %d", test_b++); 
        glViewport(0, 0, wl_.width, wl_.height);
        glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
        render(vg_, (float)wl_.width, (float)wl_.height, is_red_);
        egl_swap(&egl_);
        wl_display_flush(wl_.display);
    }

    void prepareRead() {
        while (wl_display_prepare_read(wl_.display) != 0) {
            wl_display_dispatch_pending(wl_.display);
        }
        wl_display_flush(wl_.display);
    }

    WLContext& wl_; EGLContextWl& egl_; NVGcontext* vg_;
    bool is_red_ = false;
    std::unique_ptr<IEventSource> source_;
};

int main() {
    WLContext wl; EGLContextWl egl;
    if (!wl_init(&wl, 960, 540) || !egl_init(&egl, &wl)) return 1;

    gladLoadGLES2Loader((GLADloadproc)eglGetProcAddress);
    NVGcontext* vg = nvgCreateGLES2(NVG_ANTIALIAS | NVG_STENCIL_STROKES);
    nvgCreateFont(vg, "sans", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc");

    
    Hive hive;
    auto option = hive.options();
    option.io_backend = BackendType::LINUX_POLL;
    auto worker = std::make_shared<IOWorker>(option);
    WaylandFrameWatcher watcher(*worker, wl, egl, vg);
    hive.attachIOWorker(worker);
    watcher.start();
    hive.run();
    return 0;
}
