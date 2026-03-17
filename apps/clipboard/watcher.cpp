// apps/clipboard/watcher.cpp
// Wayland clipboard monitoring via zwlr_data_control_manager_v1.
// Falls back gracefully if protocol is unavailable.
#include "watcher.h"

#include <straylight/log.h>

#include <wayland-client.h>

// wlr-data-control protocol is conditionally compiled
#ifdef HAVE_DATA_CONTROL_PROTOCOL
#include <wlr-data-control-unstable-v1-client-protocol.h>
#endif

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <vector>

namespace straylight::clipboard {

namespace {

inline SLError make_err(SLErrorCode code, const std::string& msg) {
    return SLError{code, msg};
}

// ---------------------------------------------------------------------------
// Shared watcher state passed around via Wayland user_data pointers
// ---------------------------------------------------------------------------

struct WatcherState {
    ClipHistory*  history    = nullptr;
    ClipChangedFn on_change;

    wl_display*   display    = nullptr;
    wl_registry*  registry   = nullptr;
    wl_seat*      seat       = nullptr;

#ifdef HAVE_DATA_CONTROL_PROTOCOL
    zwlr_data_control_manager_v1* dc_manager = nullptr;
    zwlr_data_control_device_v1*  dc_device  = nullptr;
#endif

    std::atomic<bool>* running = nullptr;
};

// ---------------------------------------------------------------------------
// Read all available bytes from a pipe file descriptor
// ---------------------------------------------------------------------------

std::vector<uint8_t> read_pipe_fd(int fd) {
    std::vector<uint8_t> buf;
    uint8_t tmp[4096];
    ssize_t n;
    while ((n = ::read(fd, tmp, sizeof(tmp))) > 0) {
        buf.insert(buf.end(), tmp, tmp + n);
    }
    return buf;
}

// ---------------------------------------------------------------------------
// Determine if data is valid UTF-8 text for the text/plain MIME
// ---------------------------------------------------------------------------

bool is_text_mime(const std::string& mime) {
    return mime == "text/plain" ||
           mime == "text/plain;charset=utf-8" ||
           mime == "text/plain;charset=UTF-8" ||
           mime == "UTF8_STRING" ||
           mime == "STRING" ||
           mime == "TEXT";
}

bool is_image_mime(const std::string& mime) {
    return mime.starts_with("image/");
}

#ifdef HAVE_DATA_CONTROL_PROTOCOL
// ---------------------------------------------------------------------------
// zwlr_data_control_offer_v1 — collects offered MIME types for a selection
// ---------------------------------------------------------------------------

struct OfferCtx {
    WatcherState*   ws = nullptr;
    std::vector<std::string> mimes;
};

void dc_offer_mime(void* data,
                   zwlr_data_control_offer_v1* /*offer*/,
                   const char* mime) {
    auto* ctx = static_cast<OfferCtx*>(data);
    if (mime) ctx->mimes.emplace_back(mime);
}

const zwlr_data_control_offer_v1_listener dc_offer_listener = {
    .offer = dc_offer_mime,
};

// ---------------------------------------------------------------------------
// Receive data for the best available MIME type from a data_control_offer
// ---------------------------------------------------------------------------

void receive_offer(WatcherState* ws,
                   zwlr_data_control_offer_v1* offer,
                   const std::vector<std::string>& mimes) {
    if (mimes.empty()) {
        zwlr_data_control_offer_v1_destroy(offer);
        return;
    }

    // Prefer text first
    std::string chosen_mime;
    for (const auto& m : mimes) {
        if (is_text_mime(m)) { chosen_mime = m; break; }
    }
    // Fall back to image
    if (chosen_mime.empty()) {
        for (const auto& m : mimes) {
            if (is_image_mime(m)) { chosen_mime = m; break; }
        }
    }
    if (chosen_mime.empty()) {
        zwlr_data_control_offer_v1_destroy(offer);
        return;
    }

    // Create a pipe, request the compositor to write into the write end
    int pipefd[2];
    if (::pipe2(pipefd, O_NONBLOCK) < 0) {
        zwlr_data_control_offer_v1_destroy(offer);
        return;
    }

    zwlr_data_control_offer_v1_receive(offer, chosen_mime.c_str(), pipefd[1]);
    ::close(pipefd[1]);

    // Flush so the compositor processes the receive request
    wl_display_flush(ws->display);

    // Wait up to 2 seconds for data
    struct pollfd pfd = { .fd = pipefd[0], .events = POLLIN, .revents = 0 };
    int ready = ::poll(&pfd, 1, 2000);

    std::vector<uint8_t> data;
    if (ready > 0 && (pfd.revents & POLLIN)) {
        data = read_pipe_fd(pipefd[0]);
    }
    ::close(pipefd[0]);
    zwlr_data_control_offer_v1_destroy(offer);

    if (data.empty()) return;

    if (is_text_mime(chosen_mime)) {
        std::string text(reinterpret_cast<const char*>(data.data()), data.size());
        // Strip trailing null bytes
        while (!text.empty() && text.back() == '\0') text.pop_back();
        if (text.empty()) return;

        ws->history->push_text(text, chosen_mime);

        if (ws->on_change) {
            ClipEntry e;
            e.kind = EntryKind::Text;
            e.text = text;
            e.mime = chosen_mime;
            ws->on_change(e);
        }
    } else if (is_image_mime(chosen_mime)) {
        ws->history->push_image(std::move(data), chosen_mime);

        if (ws->on_change) {
            ClipEntry e;
            e.kind = EntryKind::Image;
            e.mime = chosen_mime;
            ws->on_change(e);
        }
    }
}

// ---------------------------------------------------------------------------
// zwlr_data_control_device_v1 listener
// ---------------------------------------------------------------------------

void dc_device_data_offer(void* /*data*/,
                           zwlr_data_control_device_v1* /*device*/,
                           zwlr_data_control_offer_v1* offer) {
    // This callback fires before selection(). We attach our offer listener here.
    if (!offer) return;
    auto* ctx = new OfferCtx;
    zwlr_data_control_offer_v1_add_listener(offer, &dc_offer_listener, ctx);
}

void dc_device_selection(void* data,
                          zwlr_data_control_device_v1* /*device*/,
                          zwlr_data_control_offer_v1* offer) {
    if (!offer) return;
    auto* ws  = static_cast<WatcherState*>(data);
    auto* ctx = static_cast<OfferCtx*>(
        zwlr_data_control_offer_v1_get_user_data(offer));
    if (!ctx) {
        zwlr_data_control_offer_v1_destroy(offer);
        return;
    }
    ctx->ws = ws;
    receive_offer(ws, offer, ctx->mimes);
    delete ctx;
}

void dc_device_finished(void* /*data*/,
                         zwlr_data_control_device_v1* /*device*/) {
    // Device became invalid; the run loop will clean up.
}

void dc_device_primary_selection(void* /*data*/,
                                  zwlr_data_control_device_v1* /*device*/,
                                  zwlr_data_control_offer_v1* offer) {
    // We track the primary selection separately if desired; for now destroy it.
    if (offer) zwlr_data_control_offer_v1_destroy(offer);
}

const zwlr_data_control_device_v1_listener dc_device_listener = {
    .data_offer        = dc_device_data_offer,
    .selection         = dc_device_selection,
    .finished          = dc_device_finished,
    .primary_selection = dc_device_primary_selection,
};
#endif // HAVE_DATA_CONTROL_PROTOCOL

// ---------------------------------------------------------------------------
// Registry listener — binds wl_seat and zwlr_data_control_manager_v1
// ---------------------------------------------------------------------------

void reg_global(void* data, wl_registry* reg, uint32_t name,
                const char* iface, uint32_t ver) {
    auto* ws = static_cast<WatcherState*>(data);
    if (std::strcmp(iface, wl_seat_interface.name) == 0) {
        ws->seat = static_cast<wl_seat*>(
            wl_registry_bind(reg, name, &wl_seat_interface, std::min(ver, 5u)));
    }
#ifdef HAVE_DATA_CONTROL_PROTOCOL
    else if (std::strcmp(iface, zwlr_data_control_manager_v1_interface.name) == 0) {
        ws->dc_manager = static_cast<zwlr_data_control_manager_v1*>(
            wl_registry_bind(reg, name,
                             &zwlr_data_control_manager_v1_interface,
                             std::min(ver, 2u)));
    }
#endif
}

void reg_global_remove(void*, wl_registry*, uint32_t) {}

const wl_registry_listener reg_listener = {
    .global        = reg_global,
    .global_remove = reg_global_remove,
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// ClipWatcher::run_loop
// ---------------------------------------------------------------------------

void ClipWatcher::run_loop(wl_display* display) {
    WatcherState ws;
    ws.history   = history_;
    ws.on_change = on_change_;
    ws.display   = display;
    ws.running   = &running_;

    // Bind a second display connection for the background thread
    wl_display* thread_display = wl_display_connect(nullptr);
    if (!thread_display) {
        SL_ERROR("ClipWatcher: cannot connect to Wayland display");
        running_.store(false);
        return;
    }
    ws.display = thread_display;

    ws.registry = wl_display_get_registry(thread_display);
    wl_registry_add_listener(ws.registry, &reg_listener, &ws);
    wl_display_roundtrip(thread_display);

#ifdef HAVE_DATA_CONTROL_PROTOCOL
    if (ws.dc_manager && ws.seat) {
        ws.dc_device = zwlr_data_control_manager_v1_get_data_device(
            ws.dc_manager, ws.seat);
        zwlr_data_control_device_v1_add_listener(
            ws.dc_device, &dc_device_listener, &ws);
        wl_display_roundtrip(thread_display);
        SL_INFO("ClipWatcher: using zwlr_data_control_manager_v1");
    } else {
        SL_WARN("ClipWatcher: zwlr_data_control_manager_v1 not available, "
                "clipboard monitoring is limited");
    }
#else
    SL_WARN("ClipWatcher: data control protocol not compiled in, "
            "clipboard monitoring is limited");
#endif

    // Event loop: poll on the Wayland socket fd
    int wl_fd = wl_display_get_fd(thread_display);
    struct pollfd pfd = { .fd = wl_fd, .events = POLLIN, .revents = 0 };

    while (running_.load(std::memory_order_relaxed)) {
        wl_display_flush(thread_display);
        int rc = ::poll(&pfd, 1, 500); // 500 ms timeout so stop() is responsive
        if (rc > 0 && (pfd.revents & POLLIN)) {
            if (wl_display_dispatch(thread_display) < 0) {
                SL_ERROR("ClipWatcher: wl_display_dispatch error, stopping");
                break;
            }
        } else if (rc < 0) {
            SL_ERROR("ClipWatcher: poll error");
            break;
        }
    }

    // Cleanup
#ifdef HAVE_DATA_CONTROL_PROTOCOL
    if (ws.dc_device)  zwlr_data_control_device_v1_destroy(ws.dc_device);
    if (ws.dc_manager) zwlr_data_control_manager_v1_destroy(ws.dc_manager);
#endif
    if (ws.seat)     wl_seat_destroy(ws.seat);
    if (ws.registry) wl_registry_destroy(ws.registry);

    wl_display_disconnect(thread_display);
    running_.store(false);
    SL_INFO("ClipWatcher: thread exited");
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

Result<void, SLError> ClipWatcher::start(wl_display* display,
                                          ClipHistory& history,
                                          ClipChangedFn on_change) {
    if (running_.load()) {
        return Result<void, SLError>::error(
            make_err(SLErrorCode::AlreadyExists, "ClipWatcher already running"));
    }

    history_   = &history;
    on_change_ = std::move(on_change);
    running_.store(true);

    thread_ = std::thread([this, display] {
        run_loop(display);
    });

    return Result<void, SLError>::ok();
}

void ClipWatcher::stop() {
    running_.store(false);
    if (thread_.joinable()) thread_.join();
}

} // namespace straylight::clipboard
