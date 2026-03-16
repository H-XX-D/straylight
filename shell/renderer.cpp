// shell/renderer.cpp
// EGL + ImGui renderer implementation for Wayland surfaces
#include "renderer.h"

#include <straylight/log.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include <imgui.h>
#include <imgui_impl_opengl3.h>

namespace straylight::shell {

struct Renderer::Impl {
    EGLDisplay egl_display = EGL_NO_DISPLAY;
    EGLContext egl_context = EGL_NO_CONTEXT;
    EGLSurface egl_surface = EGL_NO_SURFACE;
    EGLConfig  egl_config  = nullptr;
    wl_egl_window* egl_window = nullptr;
    ImGuiContext* imgui_ctx = nullptr;
    int width  = 0;
    int height = 0;

    ~Impl() {
        if (imgui_ctx) {
            ImGui::SetCurrentContext(imgui_ctx);
            ImGui_ImplOpenGL3_Shutdown();
            ImGui::DestroyContext(imgui_ctx);
        }
        if (egl_display != EGL_NO_DISPLAY) {
            eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE,
                           EGL_NO_CONTEXT);
            if (egl_surface != EGL_NO_SURFACE) {
                eglDestroySurface(egl_display, egl_surface);
            }
            if (egl_context != EGL_NO_CONTEXT) {
                eglDestroyContext(egl_display, egl_context);
            }
            eglTerminate(egl_display);
        }
        if (egl_window) {
            wl_egl_window_destroy(egl_window);
        }
    }
};

Renderer::Renderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Renderer::~Renderer() = default;
Renderer::Renderer(Renderer&& other) noexcept = default;
Renderer& Renderer::operator=(Renderer&& other) noexcept = default;

Result<Renderer, SLError> Renderer::create(wl_display* display,
                                           wl_surface* surface,
                                           int width, int height) {
    if (!display || !surface) {
        return Result<Renderer, SLError>::error(
            SLError{SLErrorCode::InvalidArgument,
                    "Null Wayland display or surface"});
    }

    auto impl = std::make_unique<Impl>();
    impl->width  = width;
    impl->height = height;

    // --- EGL initialization ---
    // Use platform display extension for Wayland
    auto eglGetPlatformDisplayEXT =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
            eglGetProcAddress("eglGetPlatformDisplayEXT"));

    if (eglGetPlatformDisplayEXT) {
        impl->egl_display = eglGetPlatformDisplayEXT(
            EGL_PLATFORM_WAYLAND_EXT, display, nullptr);
    } else {
        impl->egl_display = eglGetDisplay(
            reinterpret_cast<EGLNativeDisplayType>(display));
    }

    if (impl->egl_display == EGL_NO_DISPLAY) {
        return Result<Renderer, SLError>::error(
            SLError{SLErrorCode::NotInitialized,
                    "Failed to get EGL display"});
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(impl->egl_display, &major, &minor)) {
        return Result<Renderer, SLError>::error(
            SLError{SLErrorCode::NotInitialized,
                    "eglInitialize failed"});
    }
    SL_INFO("EGL initialized: {}.{}", major, minor);

    // Choose EGL config: RGB888, depth 24, stencil 8
    constexpr EGLint config_attribs[] = {
        EGL_SURFACE_TYPE,    EGL_WINDOW_BIT,
        EGL_RED_SIZE,        8,
        EGL_GREEN_SIZE,      8,
        EGL_BLUE_SIZE,       8,
        EGL_ALPHA_SIZE,      8,
        EGL_DEPTH_SIZE,      24,
        EGL_STENCIL_SIZE,    8,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
        EGL_NONE
    };

    EGLint num_configs = 0;
    eglChooseConfig(impl->egl_display, config_attribs,
                    &impl->egl_config, 1, &num_configs);
    if (num_configs == 0) {
        return Result<Renderer, SLError>::error(
            SLError{SLErrorCode::NotInitialized,
                    "No suitable EGL config found"});
    }

    // Create wl_egl_window
    impl->egl_window = wl_egl_window_create(surface, width, height);
    if (!impl->egl_window) {
        return Result<Renderer, SLError>::error(
            SLError{SLErrorCode::NotInitialized,
                    "wl_egl_window_create failed"});
    }

    // Create EGL window surface
    impl->egl_surface = eglCreateWindowSurface(
        impl->egl_display, impl->egl_config,
        reinterpret_cast<EGLNativeWindowType>(impl->egl_window), nullptr);
    if (impl->egl_surface == EGL_NO_SURFACE) {
        return Result<Renderer, SLError>::error(
            SLError{SLErrorCode::NotInitialized,
                    "eglCreateWindowSurface failed"});
    }

    // Create OpenGL ES 3.0 context
    constexpr EGLint context_attribs[] = {
        EGL_CONTEXT_MAJOR_VERSION, 3,
        EGL_CONTEXT_MINOR_VERSION, 0,
        EGL_NONE
    };
    impl->egl_context = eglCreateContext(
        impl->egl_display, impl->egl_config,
        EGL_NO_CONTEXT, context_attribs);
    if (impl->egl_context == EGL_NO_CONTEXT) {
        return Result<Renderer, SLError>::error(
            SLError{SLErrorCode::NotInitialized,
                    "eglCreateContext failed"});
    }

    eglMakeCurrent(impl->egl_display, impl->egl_surface,
                   impl->egl_surface, impl->egl_context);

    // Vsync managed by compositor, disable swap interval
    eglSwapInterval(impl->egl_display, 0);

    // --- ImGui initialization ---
    impl->imgui_ctx = ImGui::CreateContext();
    ImGui::SetCurrentContext(impl->imgui_ctx);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width),
                            static_cast<float>(height));
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui_ImplOpenGL3_Init("#version 300 es");

    SL_INFO("Renderer created: {}x{}", width, height);
    return Result<Renderer, SLError>::ok(Renderer(std::move(impl)));
}

void Renderer::begin_frame() {
    ImGui::SetCurrentContext(impl_->imgui_ctx);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();
}

void Renderer::end_frame() {
    ImGui::Render();

    glViewport(0, 0, impl_->width, impl_->height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    eglSwapBuffers(impl_->egl_display, impl_->egl_surface);
}

void Renderer::resize(int width, int height) {
    impl_->width  = width;
    impl_->height = height;
    if (impl_->egl_window) {
        wl_egl_window_resize(impl_->egl_window, width, height, 0, 0);
    }
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(static_cast<float>(width),
                            static_cast<float>(height));
}

} // namespace straylight::shell
