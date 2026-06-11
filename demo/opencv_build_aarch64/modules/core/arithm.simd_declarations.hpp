#define CV_CPU_SIMD_FILENAME "/home/zw/嵌入式设计竞赛资料/项目代码及量化模型/demo/opencv-3.4.5/modules/core/src/arithm.simd.hpp"
#define CV_CPU_DISPATCH_MODE SSE2
#include "opencv2/core/private/cv_cpu_include_simd_declarations.hpp"

#define CV_CPU_DISPATCH_MODE SSE4_1
#include "opencv2/core/private/cv_cpu_include_simd_declarations.hpp"

#define CV_CPU_DISPATCH_MODE AVX2
#include "opencv2/core/private/cv_cpu_include_simd_declarations.hpp"

#define CV_CPU_DISPATCH_MODE VSX3
#include "opencv2/core/private/cv_cpu_include_simd_declarations.hpp"

#define CV_CPU_DISPATCH_MODES_ALL VSX3, AVX2, SSE4_1, SSE2, BASELINE

#undef CV_CPU_SIMD_FILENAME
