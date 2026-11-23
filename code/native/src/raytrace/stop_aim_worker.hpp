#pragma once
#include "detail.hpp"

namespace optiland {
using namespace raytrace_detail;

class ParallelStopAimTraceWorker {
public:
    explicit ParallelStopAimTraceWorker(const LensSystem& system)
        : tracer_(system) {
#ifdef _WIN32
        available_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        completed_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (available_ && completed_) {
            thread_ = CreateThread(
                nullptr,
                0,
                &ParallelStopAimTraceWorker::thread_entry,
                this,
                0,
                nullptr);
        }
#else
        thread_ = std::thread(&ParallelStopAimTraceWorker::run, this);
#endif
    }

    ~ParallelStopAimTraceWorker() {
#ifdef _WIN32
        if (thread_) {
            stopping_ = true;
            SetEvent(available_);
            WaitForSingleObject(thread_, INFINITE);
            CloseHandle(thread_);
        }
        if (available_) {
            CloseHandle(available_);
        }
        if (completed_) {
            CloseHandle(completed_);
        }
#else
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (busy_) {
                completed_.wait(lock, [&]() { return done_; });
                busy_ = false;
                done_ = false;
            }
            stopping_ = true;
        }
        available_.notify_one();
        if (thread_.joinable()) {
            thread_.join();
        }
#endif
    }

    bool available() const {
#ifdef _WIN32
        return thread_ != nullptr && available_ != nullptr && completed_ != nullptr;
#else
        return thread_.joinable();
#endif
    }

    void submit(
        Ray ray,
        std::size_t surface_index,
        bool ignore_intermediate_apertures) {
#ifdef _WIN32
        ray_ = std::move(ray);
        surface_index_ = surface_index;
        ignore_intermediate_apertures_ = ignore_intermediate_apertures;
        SetEvent(available_);
#else
        std::unique_lock<std::mutex> lock(mutex_);
        completed_.wait(lock, [&]() { return !busy_; });
        ray_ = std::move(ray);
        surface_index_ = surface_index;
        ignore_intermediate_apertures_ = ignore_intermediate_apertures;
        busy_ = true;
        pending_ = true;
        done_ = false;
        lock.unlock();
        available_.notify_one();
#endif
    }

    bool wait(TracePoint* point, long long* elapsed_ns) {
#ifdef _WIN32
        WaitForSingleObject(completed_, INFINITE);
        if (point) {
            *point = point_;
        }
        if (elapsed_ns) {
            *elapsed_ns = elapsed_ns_;
        }
        return ok_;
#else
        std::unique_lock<std::mutex> lock(mutex_);
        completed_.wait(lock, [&]() { return done_; });
        if (point) {
            *point = point_;
        }
        if (elapsed_ns) {
            *elapsed_ns = elapsed_ns_;
        }
        const bool ok = ok_;
        busy_ = false;
        done_ = false;
        lock.unlock();
        completed_.notify_all();
        return ok;
#endif
    }

private:
#ifdef _WIN32
    static DWORD WINAPI thread_entry(void* context) {
        static_cast<ParallelStopAimTraceWorker*>(context)->run();
        return 0;
    }
#endif

    void run() {
        for (;;) {
#ifdef _WIN32
            WaitForSingleObject(available_, INFINITE);
            if (stopping_) {
                return;
            }
            const Ray ray = ray_;
            const std::size_t surface_index = surface_index_;
            const bool ignore_intermediate_apertures =
                ignore_intermediate_apertures_;
#else
            Ray ray;
            std::size_t surface_index = 0;
            bool ignore_intermediate_apertures = false;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                available_.wait(lock, [&]() { return pending_ || stopping_; });
                if (stopping_ && !pending_) {
                    return;
                }
                ray = ray_;
                surface_index = surface_index_;
                ignore_intermediate_apertures = ignore_intermediate_apertures_;
                pending_ = false;
            }
#endif
            TracePoint point;
            const auto started = std::chrono::steady_clock::now();
            const bool ok = tracer_.trace_to_surface(
                ray,
                surface_index,
                &point,
                true,
                ignore_intermediate_apertures);
            const long long elapsed_ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started)
                    .count();
#ifdef _WIN32
            point_ = point;
            ok_ = ok;
            elapsed_ns_ = elapsed_ns;
            SetEvent(completed_);
#else
            {
                std::lock_guard<std::mutex> lock(mutex_);
                point_ = point;
                ok_ = ok;
                elapsed_ns_ = elapsed_ns;
                done_ = true;
            }
            completed_.notify_all();
#endif
        }
    }

    SequentialRayTracer tracer_;
#ifdef _WIN32
    HANDLE thread_ = nullptr;
    HANDLE available_ = nullptr;
    HANDLE completed_ = nullptr;
    volatile bool stopping_ = false;
#else
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable available_;
    std::condition_variable completed_;
    bool busy_ = false;
    bool pending_ = false;
    bool done_ = false;
    bool stopping_ = false;
#endif
    Ray ray_;
    TracePoint point_;
    std::size_t surface_index_ = 0;
    long long elapsed_ns_ = 0;
    bool ignore_intermediate_apertures_ = false;
    bool ok_ = false;
};
} // namespace optiland
