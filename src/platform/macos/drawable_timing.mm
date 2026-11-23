#include "drawable_timing.h"
#import <QuartzCore/CAMetalLayer.h>
#import <objc/runtime.h>
#include <chrono>
#include <mutex>
#include <cstring>

namespace {
std::mutex sampleMutex;
std::vector<double> samples;
IMP original = nullptr;
bool enabled = false;
id<CAMetalDrawable> timedDrawable(CAMetalLayer *layer, SEL selector)
{
    const auto begin = std::chrono::steady_clock::now();
    auto drawable = reinterpret_cast<id<CAMetalDrawable> (*)(id, SEL)>(original)(layer, selector);
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    // 仅收集Qt窗口的图层，不采集其他图层或图像内容；原方法的返回值和异常保持原样。
    if (std::strcmp(class_getName(object_getClass(layer)), "QMetalLayer") == 0) {
        std::lock_guard guard(sampleMutex);
        if (enabled && samples.size() < 1000) samples.push_back(elapsed);
    }
    return drawable;
}
}
void startDrawableTiming()
{
    std::lock_guard guard(sampleMutex);
    if (enabled) return;
    Method method = class_getInstanceMethod([CAMetalLayer class], @selector(nextDrawable));
    if (!method) return;
    original = method_getImplementation(method);
    samples.clear();
    enabled = true;
    method_setImplementation(method, reinterpret_cast<IMP>(timedDrawable));
}
void stopDrawableTiming()
{
    std::lock_guard guard(sampleMutex);
    if (!enabled) return;
    enabled = false;
    Method method = class_getInstanceMethod([CAMetalLayer class], @selector(nextDrawable));
    // 不覆盖在本探针之后安装的其他诊断；进程中始终保留原函数指针供在途调用返回。
    if (method && method_getImplementation(method) == reinterpret_cast<IMP>(timedDrawable))
        method_setImplementation(method, original);
}
std::vector<double> takeDrawableTimings()
{
    std::lock_guard guard(sampleMutex);
    std::vector<double> result;
    result.swap(samples);
    return result;
}
