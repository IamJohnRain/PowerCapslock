#include "audio.h"
#include "logger.h"
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <stdio.h>
#include <stdlib.h>

// 定义 GUID（MinGW 需要）
#ifdef __MINGW32__
static const GUID CLSID_MMDeviceEnumerator_Value = {0xbcde0395, 0xe52f, 0x467c, {0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e}};
static const GUID IID_IMMDeviceEnumerator_Value = {0xa95664d2, 0x9614, 0x4f35, {0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6}};
static const GUID IID_IAudioClient_Value = {0x1cb9ad4c, 0xdbfa, 0x4c32, {0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2}};
static const GUID IID_IAudioCaptureClient_Value = {0xc8adbd64, 0xe71e, 0x48a0, {0xa4, 0xde, 0x18, 0x5c, 0x39, 0x5c, 0xd3, 0x17}};
#define CLSID_MMDeviceEnumerator CLSID_MMDeviceEnumerator_Value
#define IID_IMMDeviceEnumerator IID_IMMDeviceEnumerator_Value
#define IID_IAudioClient IID_IAudioClient_Value
#define IID_IAudioCaptureClient IID_IAudioCaptureClient_Value
#endif

// WASAPI 相关常量
#define REFTIMES_PER_SEC  10000000
#define REFTIMES_PER_MILLISEC  10000
#define TARGET_SAMPLE_RATE 16000  // 目标采样率

// 全局状态
static IMMDeviceEnumerator* g_enumerator = NULL;
static IMMDevice* g_device = NULL;
static IAudioClient* g_audioClient = NULL;
static IAudioCaptureClient* g_captureClient = NULL;
static HANDLE g_event = NULL;

// 录音缓冲区
static float* g_recordBuffer = NULL;
static int g_bufferSize = 0;        // 当前缓冲区大小（样本数）
static int g_bufferCapacity = 0;    // 缓冲区容量（样本数）
static bool g_isRecording = false;

// 扩展缓冲区
static bool ExpandBuffer(int requiredSize) {
    if (requiredSize <= g_bufferCapacity) {
        return true;
    }

    int newCapacity = g_bufferCapacity == 0 ? TARGET_SAMPLE_RATE : g_bufferCapacity * 2;
    while (newCapacity < requiredSize) {
        newCapacity *= 2;
    }

    float* newBuffer = (float*)realloc(g_recordBuffer, newCapacity * sizeof(float));
    if (newBuffer == NULL) {
        LOG_ERROR("音频缓冲区分配失败");
        return false;
    }

    g_recordBuffer = newBuffer;
    g_bufferCapacity = newCapacity;
    LOG_DEBUG("音频缓冲区扩展至 %d 样本", newCapacity);
    return true;
}

bool AudioInit(void) {
    HRESULT hr;

    // 初始化 COM
    hr = CoInitialize(NULL);
    if (FAILED(hr)) {
        LOG_ERROR("COM 初始化失败: 0x%08X", hr);
        return false;
    }

    // 创建设备枚举器
    hr = CoCreateInstance(
        &CLSID_MMDeviceEnumerator,
        NULL,
        CLSCTX_ALL,
        &IID_IMMDeviceEnumerator,
        (void**)&g_enumerator
    );

    if (FAILED(hr)) {
        LOG_ERROR("创建设备枚举器失败: 0x%08X", hr);
        CoUninitialize();
        return false;
    }

    // 获取默认音频输入设备
    hr = g_enumerator->lpVtbl->GetDefaultAudioEndpoint(
        g_enumerator,
        eCapture,
        eConsole,
        &g_device
    );

    if (FAILED(hr)) {
        LOG_ERROR("获取默认音频输入设备失败: 0x%08X", hr);
        g_enumerator->lpVtbl->Release(g_enumerator);
        g_enumerator = NULL;
        CoUninitialize();
        return false;
    }

    // 创建事件
    g_event = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (g_event == NULL) {
        LOG_ERROR("创建事件失败");
        g_device->lpVtbl->Release(g_device);
        g_device = NULL;
        g_enumerator->lpVtbl->Release(g_enumerator);
        g_enumerator = NULL;
        CoUninitialize();
        return false;
    }

    LOG_INFO("音频模块初始化成功");
    return true;
}

void AudioCleanup(void) {
    if (g_isRecording) {
        AudioStopRecording(NULL, NULL);
    }

    if (g_captureClient != NULL) {
        g_captureClient->lpVtbl->Release(g_captureClient);
        g_captureClient = NULL;
    }

    if (g_audioClient != NULL) {
        g_audioClient->lpVtbl->Release(g_audioClient);
        g_audioClient = NULL;
    }

    if (g_event != NULL) {
        CloseHandle(g_event);
        g_event = NULL;
    }

    if (g_device != NULL) {
        g_device->lpVtbl->Release(g_device);
        g_device = NULL;
    }

    if (g_enumerator != NULL) {
        g_enumerator->lpVtbl->Release(g_enumerator);
        g_enumerator = NULL;
    }

    if (g_recordBuffer != NULL) {
        free(g_recordBuffer);
        g_recordBuffer = NULL;
    }

    g_bufferSize = 0;
    g_bufferCapacity = 0;

    CoUninitialize();
    LOG_DEBUG("音频资源已清理");
}

bool AudioStartRecording(void) {
    if (g_device == NULL) {
        LOG_ERROR("音频设备未初始化");
        return false;
    }

    if (g_isRecording) {
        LOG_WARN("已在录音中");
        return true;
    }

    HRESULT hr;

    // 激活音频客户端
    hr = g_device->lpVtbl->Activate(
        g_device,
        &IID_IAudioClient,
        CLSCTX_ALL,
        NULL,
        (void**)&g_audioClient
    );

    if (FAILED(hr)) {
        LOG_ERROR("激活音频客户端失败: 0x%08X", hr);
        return false;
    }

    // 获取设备格式
    WAVEFORMATEX* deviceFormat = NULL;
    hr = g_audioClient->lpVtbl->GetMixFormat(g_audioClient, &deviceFormat);
    if (FAILED(hr)) {
        LOG_ERROR("获取设备格式失败: 0x%08X", hr);
        g_audioClient->lpVtbl->Release(g_audioClient);
        g_audioClient = NULL;
        return false;
    }

    LOG_DEBUG("设备格式: %d Hz, %d 通道, %d 位",
              deviceFormat->nSamplesPerSec,
              deviceFormat->nChannels,
              deviceFormat->wBitsPerSample);

    // 初始化音频客户端
    // 使用 1 秒的缓冲区
    REFERENCE_TIME bufferDuration = REFTIMES_PER_SEC;

    hr = g_audioClient->lpVtbl->Initialize(
        g_audioClient,
        AUDCLNT_SHAREMODE_SHARED,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        bufferDuration,
        0,
        deviceFormat,
        NULL
    );

    if (FAILED(hr)) {
        LOG_ERROR("初始化音频客户端失败: 0x%08X", hr);
        CoTaskMemFree(deviceFormat);
        g_audioClient->lpVtbl->Release(g_audioClient);
        g_audioClient = NULL;
        return false;
    }

    // 设置事件回调
    hr = g_audioClient->lpVtbl->SetEventHandle(g_audioClient, g_event);
    if (FAILED(hr)) {
        LOG_ERROR("设置事件句柄失败: 0x%08X", hr);
        CoTaskMemFree(deviceFormat);
        g_audioClient->lpVtbl->Release(g_audioClient);
        g_audioClient = NULL;
        return false;
    }

    // 获取捕获客户端
    hr = g_audioClient->lpVtbl->GetService(
        g_audioClient,
        &IID_IAudioCaptureClient,
        (void**)&g_captureClient
    );

    if (FAILED(hr)) {
        LOG_ERROR("获取捕获客户端失败: 0x%08X", hr);
        CoTaskMemFree(deviceFormat);
        g_audioClient->lpVtbl->Release(g_audioClient);
        g_audioClient = NULL;
        return false;
    }

    // 开始录音
    hr = g_audioClient->lpVtbl->Start(g_audioClient);
    if (FAILED(hr)) {
        LOG_ERROR("开始录音失败: 0x%08X", hr);
        g_captureClient->lpVtbl->Release(g_captureClient);
        g_captureClient = NULL;
        CoTaskMemFree(deviceFormat);
        g_audioClient->lpVtbl->Release(g_audioClient);
        g_audioClient = NULL;
        return false;
    }

    // 保存设备格式信息用于后续转换
    // 简化处理：假设设备格式可以直接使用
    // 实际应用中可能需要使用重采样库

    CoTaskMemFree(deviceFormat);

    // 重置缓冲区
    g_bufferSize = 0;

    g_isRecording = true;
    LOG_INFO("开始录音");
    return true;
}

bool AudioStopRecording(float** samples, int* numSamples) {
    if (!g_isRecording) {
        LOG_WARN("未在录音中");
        return false;
    }

    g_isRecording = false;

    HRESULT hr;

    // 停止录音
    hr = g_audioClient->lpVtbl->Stop(g_audioClient);
    if (FAILED(hr)) {
        LOG_WARN("停止录音失败: 0x%08X", hr);
    }

    // 读取剩余数据
    UINT32 packetLength;
    BYTE* data;
    UINT32 numFramesAvailable;
    DWORD flags;

    while (true) {
        hr = g_captureClient->lpVtbl->GetNextPacketSize(g_captureClient, &packetLength);
        if (FAILED(hr) || packetLength == 0) {
            break;
        }

        hr = g_captureClient->lpVtbl->GetBuffer(
            g_captureClient,
            &data,
            &numFramesAvailable,
            &flags,
            NULL,
            NULL
        );

        if (SUCCEEDED(hr)) {
            // 扩展缓冲区
            if (!ExpandBuffer(g_bufferSize + (int)numFramesAvailable)) {
                break;
            }

            // 复制数据（假设是 float 格式）
            // 注意：实际设备格式可能不同，需要重采样
            float* floatData = (float*)data;
            for (UINT32 i = 0; i < numFramesAvailable; i++) {
                // 简化：取第一个通道
                g_recordBuffer[g_bufferSize++] = floatData[i];
            }

            g_captureClient->lpVtbl->ReleaseBuffer(g_captureClient, numFramesAvailable);
        }
    }

    // 清理资源
    g_captureClient->lpVtbl->Release(g_captureClient);
    g_captureClient = NULL;
    g_audioClient->lpVtbl->Release(g_audioClient);
    g_audioClient = NULL;

    LOG_INFO("录音结束，共 %d 样本 (%.2f 秒)", g_bufferSize, (float)g_bufferSize / TARGET_SAMPLE_RATE);

    // 返回结果
    if (samples != NULL && numSamples != NULL) {
        *samples = g_recordBuffer;
        *numSamples = g_bufferSize;
        g_recordBuffer = NULL;
        g_bufferSize = 0;
        g_bufferCapacity = 0;
    } else {
        // 调用者不需要数据，清空缓冲区
        free(g_recordBuffer);
        g_recordBuffer = NULL;
        g_bufferSize = 0;
        g_bufferCapacity = 0;
    }

    return true;
}

bool AudioIsRecording(void) {
    return g_isRecording;
}
