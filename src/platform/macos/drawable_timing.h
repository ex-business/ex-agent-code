#pragma once
#include <vector>
// 诊断专用：CAMetalLayer缓冲区获取的墙钟耗时，不等同于纯GPU执行或信号量等待。
void startDrawableTiming();
void stopDrawableTiming();
std::vector<double> takeDrawableTimings();
