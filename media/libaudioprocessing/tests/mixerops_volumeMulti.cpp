/*
 * Copyright 2024 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <inttypes.h>
#include <type_traits>
#define LOG_ALWAYS_FATAL(...)

#include <../AudioMixerOps.h>
#include <benchmark/benchmark.h>

using namespace android;

/*
Pixel 5(USE_NEON code) 1920 frames
mixerops_volumeMulti:
-------
arm64-v8a mixerops_volumeMulti: Passed: 4, Failed: 0, Ignored: 0, Assumption Failed: 0  
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
Benchmark                            Time                      CPU             Iteration
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
mixerops_volumeMulti:
  #BM_VolumeMulti_8_Mono1    1875.6377387923758 ns    1864.0415057668213 ns       391030
  #BM_VolumeMulti_8_Mono2    1602.6841117993513 ns    1593.2500333711928 ns       427015
  #BM_VolumeMulti_8_Mono3     1788.224978951734 ns    1777.6101210923541 ns       400273
  #BM_VolumeMulti_8_Mono4    1636.1004754499193 ns    1626.2864829750768 ns       429488

Pixel 5(generic intrinsics) code 1920 frames
mixerops_volumeMulti:
-------
arm64-v8a mixerops_volumeMulti: Passed: 4, Failed: 0, Ignored: 0, Assumption Failed: 0  
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
Benchmark                            Time                      CPU             Iteration
-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------
mixerops_volumeMulti:
  #BM_VolumeMulti_8_Mono1    2044.8808443933444 ns    2032.4622396062234 ns       341310
  #BM_VolumeMulti_8_Mono2    1680.2099143730866 ns     1670.547770077418 ns       419028
  #BM_VolumeMulti_8_Mono3    2032.6226195887816 ns    2021.2044916460075 ns       340855
  #BM_VolumeMulti_8_Mono4      1680.08664803375 ns    1670.5781081493988 ns       445088
*/

static void BM_VolumeMulti_8_Mono1(benchmark::State& state) {
    constexpr size_t FRAME_COUNT = 1920;
    constexpr size_t SAMPLE_COUNT = FRAME_COUNT * 8; // NCHAN = 8

    float out[SAMPLE_COUNT]{};
    float in[SAMPLE_COUNT]{};
    float aux[FRAME_COUNT]{};
    float vola = 0.f;
    float vol[2] = {0.f, 0.f};

    while (state.KeepRunning()) {
        benchmark::DoNotOptimize(out);
        benchmark::DoNotOptimize(in);
        volumeMulti<MIXTYPE_MULTI_MONOVOL, 8, float, float, float, float, float>(out, FRAME_COUNT, in, NULL, vol, vola);
        benchmark::ClobberMemory();
    }
}

static void BM_VolumeMulti_8_Mono2(benchmark::State& state) {
    constexpr size_t FRAME_COUNT = 1920;
    constexpr size_t SAMPLE_COUNT = FRAME_COUNT * 8; // NCHAN = 8

    float out[SAMPLE_COUNT]{};
    float in[SAMPLE_COUNT]{};
    float aux[FRAME_COUNT]{};
    float vola = 0.f;
    float vol[2] = {0.f, 0.f};

    while (state.KeepRunning()) {
        benchmark::DoNotOptimize(out);
        benchmark::DoNotOptimize(in);
        volumeMulti<MIXTYPE_MULTI_SAVEONLY_MONOVOL, 8, float, float, float, float, float>(out, FRAME_COUNT, in, NULL, vol, vola);
        benchmark::ClobberMemory();
    }
}

static void BM_VolumeMulti_8_Mono3(benchmark::State& state) {
    constexpr size_t FRAME_COUNT = 1920;
    constexpr size_t SAMPLE_COUNT = FRAME_COUNT * 8; // NCHAN = 8

    float out[SAMPLE_COUNT]{};
    float in[SAMPLE_COUNT] = {1.5};
    float aux[FRAME_COUNT]{};
    float vola = 0.f;
    float vol[2] = {0.f, 0.f};

    while (state.KeepRunning()) {
        benchmark::DoNotOptimize(out);
        benchmark::DoNotOptimize(in);
        volumeMulti<MIXTYPE_MULTI_STEREOVOL, 8, float, float, float, float, float>(out, FRAME_COUNT, in, NULL, vol, vola);
        benchmark::ClobberMemory();
    }
}

static void BM_VolumeMulti_8_Mono4(benchmark::State& state) {
    constexpr size_t FRAME_COUNT = 1920;
    constexpr size_t SAMPLE_COUNT = FRAME_COUNT * 8; // NCHAN = 8

    float out[SAMPLE_COUNT]{};
    float in[SAMPLE_COUNT] = {1.5};
    float aux[FRAME_COUNT]{};
    float vola = 0.f;
    float vol[2] = {0.f, 0.f};

    while (state.KeepRunning()) {
        benchmark::DoNotOptimize(out);
        benchmark::DoNotOptimize(in);
        volumeMulti<MIXTYPE_MULTI_SAVEONLY_STEREOVOL, 8, float, float, float, float, float>(out, FRAME_COUNT, in, NULL, vol, vola);
        benchmark::ClobberMemory();
    }
}

BENCHMARK(BM_VolumeMulti_8_Mono1);
BENCHMARK(BM_VolumeMulti_8_Mono2);
BENCHMARK(BM_VolumeMulti_8_Mono3);
BENCHMARK(BM_VolumeMulti_8_Mono4);

BENCHMARK_MAIN();
