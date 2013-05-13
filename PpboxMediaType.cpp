//////////////////////////////////////////////////////////////////////////
//
// PpboxMediaSink.h
// Implements the Ppbox media Sinkobject.
//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) Microsoft Corporation. All rights reserved.
//
//////////////////////////////////////////////////////////////////////////


#include "StdAfx.h"
#include <InitGuid.h>
#include <wmcodecdsp.h>

#include "Windows.h"
#include "Windows.Media.h"

#include "SafeRelease.h"
#include "Trace.h"

#define PPBOX_IMPORT_FUNC
#include <plugins/ppbox/ppbox_dynamic.h>

#include "PpboxMediaType.h"

using namespace ABI::Windows::Foundation;
using namespace ABI::Windows::Foundation::Collections;
using namespace ABI::Windows::Media::MediaProperties;

/*  Static functions */
static HRESULT AddAttribute( _In_ GUID guidKey, _In_ IPropertyValue *pValue, _In_ IMFAttributes* pAttr )
{
    HRESULT hr = S_OK;
    PROPVARIANT var;
    PropertyType type;
    hr = pValue->get_Type(&type);
    ZeroMemory(&var, sizeof(var));

    if (SUCCEEDED(hr))
    {
        switch( type )
        {
        case PropertyType_UInt8Array:
            {
                UINT32 cbBlob;
                BYTE *pbBlog = nullptr;
                hr = pValue->GetUInt8Array( &cbBlob, &pbBlog );
                if (SUCCEEDED(hr))
                {
                    if (pbBlog == nullptr)
                    {
                        hr = E_INVALIDARG;
                    }
                    else
                    {
                        hr = pAttr->SetBlob( guidKey, pbBlog, cbBlob );
                    }
                }
                CoTaskMemFree( pbBlog );
            }
            break;

        case PropertyType_Double:
            {
                DOUBLE value;
                hr = pValue->GetDouble(&value);
                if (SUCCEEDED(hr))
                {
                    hr = pAttr->SetDouble(guidKey, value);
                }
            }
            break;

        case PropertyType_Guid:
            {
                GUID value;
                hr = pValue->GetGuid( &value );
                if (SUCCEEDED(hr))
                {
                    hr = pAttr->SetGUID(guidKey, value);
                }
            }
            break;

        case PropertyType_String:
            {
                HString value;
                hr = pValue->GetString(value.GetAddressOf());
                if (SUCCEEDED(hr))
                {
                    UINT32 len = 0;
                    LPCWSTR szValue = WindowsGetStringRawBuffer(value.Get(), &len);
                    hr = pAttr->SetString( guidKey, szValue);
                }
            }
            break;

        case PropertyType_UInt32:
            {
                UINT32 value;
                hr = pValue->GetUInt32(&value);
                if (SUCCEEDED(hr))
                {
                    pAttr->SetUINT32(guidKey, value);
                }
            }
            break;

        case PropertyType_UInt64:
            {
                UINT64 value;
                hr = pValue->GetUInt64(&value);
                if (SUCCEEDED(hr))
                {
                    hr = pAttr->SetUINT64(guidKey, value);
                }
            }
            break;

        case PropertyType_Inspectable:
            {
                ComPtr<IInspectable> value;
                hr = TYPE_E_TYPEMISMATCH;
                if (SUCCEEDED(hr))
                {
                    pAttr->SetUnknown(guidKey, value.Get());
                }
            }
            break;

            // ignore unknown values
        }
    }

    return hr;
}

HRESULT ConvertPropertiesToMediaType(_In_ IMediaEncodingProperties *pMEP, _Outptr_ IMFMediaType **ppMT)
{
    HRESULT hr = S_OK;
    ComPtr<IMFMediaType> spMT;
    ComPtr<IMap<GUID, IInspectable*>> spMap;
    ComPtr<IIterable<IKeyValuePair<GUID, IInspectable*>*>> spIterable;
    ComPtr<IIterator<IKeyValuePair<GUID, IInspectable*>*>> spIterator;

    if (pMEP == nullptr || ppMT == nullptr)
    {
        return E_INVALIDARG;
    }
    *ppMT = nullptr;

    hr = pMEP->get_Properties(&spMap);

    if (SUCCEEDED(hr))
    {
        hr = spMap.As(&spIterable);
    }
    if (SUCCEEDED(hr))
    {
        hr = spIterable->First(&spIterator);
    }
    if (SUCCEEDED(hr))
    {
        MFCreateMediaType(&spMT);
    }

    boolean hasCurrent = false;
    if (SUCCEEDED(hr))
    {
        hr = spIterator->get_HasCurrent(&hasCurrent);
    }

    while (hasCurrent)
    {
        ComPtr<IKeyValuePair<GUID, IInspectable*> > spKeyValuePair;
        ComPtr<IInspectable> spValue;
        ComPtr<IPropertyValue> spPropValue;
        GUID guidKey;

        hr = spIterator->get_Current(&spKeyValuePair);
        if (FAILED(hr))
        {
            break;
        }
        hr = spKeyValuePair->get_Key(&guidKey);
        if (FAILED(hr))
        {
            break;
        }
        hr = spKeyValuePair->get_Value(&spValue);
        if (FAILED(hr))
        {
            break;
        }
        hr = spValue.As(&spPropValue);
        if (FAILED(hr))
        {
            break;
        }
        hr = AddAttribute(guidKey, spPropValue.Get(), spMT.Get());
        if (FAILED(hr))
        {
            break;
        }

        hr = spIterator->MoveNext(&hasCurrent);
        if (FAILED(hr))
        {
            break;
        }
    }


    if (SUCCEEDED(hr))
    {
        ComPtr<IInspectable> spValue;
        ComPtr<IPropertyValue> spPropValue;
        GUID guiMajorType;

        hr = spMap->Lookup(MF_MT_MAJOR_TYPE, spValue.GetAddressOf());

        if (SUCCEEDED(hr))
        {
            hr = spValue.As(&spPropValue);
        }
        if (SUCCEEDED(hr))
        {
            hr = spPropValue->GetGuid(&guiMajorType);
        }
        if (SUCCEEDED(hr))
        {
            if (guiMajorType != MFMediaType_Video && guiMajorType != MFMediaType_Audio)
            {
                hr = E_UNEXPECTED;
            }
        }
    }

    if (SUCCEEDED(hr))
    {
        *ppMT = spMT.Detach();
    }

    return hr;
}

HRESULT ConvertConfigurationsToMediaTypes(
    IPropertySet *pConfigurations, 
    ComPtrList<IMFMediaType> * pListMT)
{
    HRESULT hr = S_OK;
    ComPtr<IPropertySet> sConfigurations(pConfigurations);
    ComPtr<IMap<HSTRING, IInspectable*>> spMap;
    HSTRING hKey = NULL;
    ComPtr<IInspectable> spValue;
    ComPtr<IIterable<IMediaEncodingProperties*>> spIterable;
    ComPtr<IIterator<IMediaEncodingProperties*>> spIterator;

    if (pConfigurations == nullptr || pListMT == nullptr)
    {
        return E_INVALIDARG;
    }
   
    pListMT->Clear();

    hr = sConfigurations.As(&spMap);

    if (SUCCEEDED(hr))
    {
        WindowsCreateString(L"MediaEncodingProfile", 20, &hKey);
    }
    if (SUCCEEDED(hr))
    {
        hr = spMap->Lookup(hKey, &spValue);
    }
    if (SUCCEEDED(hr))
    {
        hr = WindowsDeleteString(hKey);
    }
    if (SUCCEEDED(hr))
    {
        hr = spValue.As(&spIterable);
    }
    if (SUCCEEDED(hr))
    {
        hr = spIterable->First(&spIterator);
    }
    boolean hasCurrent = false;
    if (SUCCEEDED(hr))
    {
        hr = spIterator->get_HasCurrent(&hasCurrent);
    }

    while (hasCurrent)
    {
        ComPtr<IMediaEncodingProperties> spProperties;
        ComPtr<IMFMediaType> spMT;

        hr = spIterator->get_Current(&spProperties);
        if (FAILED(hr))
        {
            break;
        }

        hr = ConvertPropertiesToMediaType(spProperties.Get(), &spMT);
        if (FAILED(hr))
        {
            break;
        }

        PrintMediaType(spMT.Get());

        hr = pListMT->InsertBack(spMT.Get());
        if (FAILED(hr))
        {
            break;
        }

        hr = spIterator->MoveNext(&hasCurrent);
        if (FAILED(hr))
        {
            break;
        }
    }

    return hr;
}

HRESULT GetDestinationtFromConfigurations(
    ABI::Windows::Foundation::Collections::IPropertySet *pConfigurations, 
    HSTRING * pDestination)
{
    HRESULT hr = S_OK;
    ComPtr<IPropertySet> sConfigurations(pConfigurations);
    ComPtr<IMap<HSTRING, IInspectable*>> spMap;
    HSTRING hKey = NULL;
    ComPtr<IInspectable> spValue;
    ComPtr<IPropertyValue> spDestination;

    if (pConfigurations == nullptr || pDestination == nullptr)
    {
        return E_INVALIDARG;
    }
   
    hr = sConfigurations.As(&spMap);

    if (SUCCEEDED(hr))
    {
        WindowsCreateString(L"Destination", 11, &hKey);
    }
    if (SUCCEEDED(hr))
    {
        hr = spMap->Lookup(hKey, &spValue);
    }
    if (SUCCEEDED(hr))
    {
        hr = WindowsDeleteString(hKey);
    }
    if (SUCCEEDED(hr))
    {
        hr = spValue.As(&spDestination);
    }
    if (SUCCEEDED(hr))
    {
        spDestination->GetString(pDestination);
    }
    return hr;
}


//-------------------------------------------------------------------
// CreateVideoMediaType:
// Create a media type from an Ppbox video sequence header.
//-------------------------------------------------------------------

HRESULT CreateVideoMediaType(PPBOX_StreamInfo& info, IMFMediaType *pType)
{
    HRESULT hr = S_OK;

    if (SUCCEEDED(hr))
    {
        GUID sub_type;
        hr = pType->GetGUID(MF_MT_SUBTYPE, &sub_type);
        if (SUCCEEDED(hr))
        {
            if (sub_type == MFVideoFormat_H264) {
                info.sub_type = PPBOX_VideoSubType_AVC1;
                info.format_type = PPBOX_FormatType_video_avc_byte_stream;
                if (SUCCEEDED(hr))
                {
                    // sequence header
                    UINT8 * buf = new UINT8[256];
                    UINT32 len = 0;
                    hr = pType->GetBlob(
                        MF_MT_MPEG_SEQUENCE_HEADER,
                        buf,
                        256, 
                        &len);
                    if (SUCCEEDED(hr)) {
                        info.format_size = len;
                        info.format_buffer = buf;
                    } else {
                        hr = S_OK;
                        delete buf;
                    }
                }
            } else if (sub_type == MFVideoFormat_WMV3) {
                info.sub_type = PPBOX_VideoSubType_WMV3;
                info.format_type = PPBOX_FormatType_none;
            } else {
                hr = MF_E_INVALIDTYPE;
            }
        }
    }

    // Format details.
    if (SUCCEEDED(hr))
    {
        // Frame size
        UINT32 width = 0;
        UINT32 height = 0;
        hr = MFGetAttributeSize(
            pType,
            MF_MT_FRAME_SIZE,
            &width,
            &height
            );
        if (SUCCEEDED(hr))
        {
            info.video_format.width = width;
            info.video_format.height = height;
        }
    }

    if (SUCCEEDED(hr))
    {
        // Frame rate

        UINT32 N = 0;
        UINT32 D = 0;
        hr = MFGetAttributeRatio(
            pType,
            MF_MT_FRAME_RATE,
            &N,
            &D
            );
        if (SUCCEEDED(hr))
        {
            info.video_format.frame_rate = N / D;
        }
    }

    return hr;
}

//*
HRESULT CreateAudioMediaType(PPBOX_StreamInfo& info, IMFMediaType *pType)
{
    HRESULT hr = S_OK;

    // Subtype = Ppbox payload
    if (SUCCEEDED(hr))
    {
        GUID sub_type;
        hr = pType->GetGUID(MF_MT_SUBTYPE, &sub_type);
        if (SUCCEEDED(hr))
        {
            if (sub_type == MFAudioFormat_AAC) {
                info.sub_type = PPBOX_AudioSubType_MP4A;
                info.format_type = PPBOX_FormatType_audio_raw;
                if (info.format_size > sizeof(HEAACWAVEINFO) - sizeof(WAVEFORMATEX))
                {
                    info.format_size -= sizeof(HEAACWAVEINFO) - sizeof(WAVEFORMATEX);
                    info.format_buffer += sizeof(HEAACWAVEINFO) - sizeof(WAVEFORMATEX);
                }
            } else if (sub_type == MFAudioFormat_MP3) {
                info.sub_type = PPBOX_AudioSubType_MP1A;
                info.format_type = PPBOX_FormatType_audio_raw;
            } else if (sub_type == MFAudioFormat_WMAudioV8) {
                info.sub_type = PPBOX_AudioSubType_WMA2;
                info.format_type = PPBOX_FormatType_none;
            } else {
                hr = MF_E_INVALIDTYPE;
            }
        }
    }

    // Format details.
    if (SUCCEEDED(hr))
    {
        // Sample size

        UINT32 N;
        hr = pType->GetUINT32(
            MF_MT_AUDIO_BITS_PER_SAMPLE,
            &N
            );
        if (SUCCEEDED(hr))
        {
            info.audio_format.sample_size = N;
        }
    }

    if (SUCCEEDED(hr))
    {
        // Channel count

        UINT32 N;
        hr = pType->GetUINT32(
            MF_MT_AUDIO_NUM_CHANNELS,
            &N
            );
        if (SUCCEEDED(hr))
        {
            info.audio_format.channel_count = N;
        }
    }

    if (SUCCEEDED(hr))
    {
        // Sample rate

        UINT32 N;
        hr = pType->GetUINT32(
            MF_MT_AUDIO_SAMPLES_PER_SECOND,
            &N
            );
        if (SUCCEEDED(hr))
        {
            info.audio_format.sample_rate = N;
        }
    }

    return hr;
}

HRESULT CreateMediaType(PPBOX_StreamInfo& info, IMFMediaType *pType)
{
    HRESULT hr = S_OK;
    memset(&info, 0, sizeof(info));

    if (SUCCEEDED(hr))
    {
        GUID major;
        hr = pType->GetGUID(MF_MT_MAJOR_TYPE, &major);
        if (SUCCEEDED(hr)) {
            if (major == MFMediaType_Video) {
                info.type = PPBOX_StreamType_VIDE;
            } else if (major == MFMediaType_Audio) {
                info.type = PPBOX_StreamType_AUDI;
            } else {
                hr = MF_E_INVALIDTYPE;
            }
            info.time_scale = 10 * 1000 * 1000;
        }
    }

    if (SUCCEEDED(hr))
    {
        // foramt data
        UINT8 * buf = new UINT8[256];
        UINT32 len = 0;
        hr = pType->GetBlob(
            MF_MT_USER_DATA,
            buf,
            256, 
            &len);
        if (SUCCEEDED(hr)) {
            info.format_size = len;
            info.format_buffer = buf;
        } else {
            hr = S_OK;
            delete buf;
        }
    }

    // Subtype = Ppbox payload
    if (SUCCEEDED(hr))
    {
        if (info.type == PPBOX_StreamType_VIDE)
            hr = CreateVideoMediaType(info, pType);
        else if (info.type == PPBOX_StreamType_AUDI)
            hr = CreateAudioMediaType(info, pType);
    }

    return hr;
}

static DWORD SampleCount = 0;
static DWORD LockSampleCount = 0;

HRESULT CreateSample(PPBOX_Sample& sample, IMFSample *pSample)
{
    HRESULT hr = S_OK;

    memset(&sample, 0, sizeof(sample));

    if (SUCCEEDED(hr))
    {
        // sync
        UINT32 N = 0;
        hr = pSample->GetUINT32
            (MFSampleExtension_CleanPoint, 
            &N);
        if (SUCCEEDED(hr) && N)
        {
            sample.flags |= PPBOX_SampleFlag_sync;
        }
        else
        {
            hr = S_OK;
        }
    }

    if (SUCCEEDED(hr))
    {
        // discontinuity
        UINT32 N = 0;
        hr = pSample->GetUINT32
            (MFSampleExtension_Discontinuity, 
            &N);
        if (SUCCEEDED(hr) && N)
        {
            sample.flags |= PPBOX_SampleFlag_discontinuity;
        }
        else
        {
            hr = S_OK;
        }
    }

    if (SUCCEEDED(hr))
    {
        // time
        INT64 time = 0;
        hr = pSample->GetSampleTime(
            &time);
        if (SUCCEEDED(hr))
        {
            sample.decode_time = time;
        }
    }

    if (SUCCEEDED(hr))
    {
        // duration
        INT64 duration = 0;
        hr = pSample->GetSampleDuration(
            &duration);
        if (SUCCEEDED(hr))
        {
            sample.duration = (PP_uint32)duration;
        }
    }

    if (SUCCEEDED(hr))
    {
        // size
        DWORD size = 0;
        hr = pSample->GetTotalLength(
            &size);
        if (SUCCEEDED(hr))
        {
            sample.size = size;
        }
    }

    if (SUCCEEDED(hr))
    {
        // cbuf
        DWORD dwBufferCount = 0;
        hr = pSample->GetBufferCount(&dwBufferCount);
        if (SUCCEEDED(hr))
        {
            if (dwBufferCount == 1) {
                IMFMediaBuffer *pBuffer = NULL;
                BYTE *pData = NULL;      // Pointer to the IMFMediaBuffer data.
                DWORD dwSize = 0;
                hr = pSample->GetBufferByIndex(0, &pBuffer);
                if (SUCCEEDED(hr))
                {
                    hr = pBuffer->Lock(&pData, NULL, &dwSize);
                }
                if (SUCCEEDED(hr))
                {
                    assert(dwSize == sample.size);
                    sample.buffer = pData;
                }
                SafeRelease(&pBuffer);
            } else {
                sample.size = dwBufferCount;
                sample.buffer = NULL;
            }
        }
    }

    sample.context = pSample;
    pSample->AddRef();

    ++SampleCount;
    ++LockSampleCount;

    if (SampleCount % 10 == 0)
        TRACE(0, L"SampleCount = %u - %u\r\n", SampleCount, LockSampleCount);

    TRACEHR_RET(hr);
}

bool GetSampleBuffers(void const *context, PPBOX_SampleBuffer * buffers)
{
    HRESULT hr = S_OK;
    IMFMediaBuffer      *pBuffer = NULL;
    IMFSample           *pSample = (IMFSample *)context;
    DWORD               dwBufferCount = 0;
    BYTE                *pData = NULL;      // Pointer to the IMFMediaBuffer data.
    DWORD               dwSize = 0;
    DWORD               dwTotalSize = 0;

    hr = pSample->GetBufferCount(&dwBufferCount);

    if (FAILED(hr))
    {
        return false;
    }

    for (DWORD i = 0; i < dwBufferCount; ++i)
    {
        hr = pSample->GetBufferByIndex(i, &pBuffer);
        if (FAILED(hr))
        {
            break;
        }
        hr = pBuffer->Lock(&pData, NULL, &dwSize);
        if (FAILED(hr))
        {
            SafeRelease(&pBuffer);
            break;
        }
        buffers[i].data = pData;
        buffers[i].len = dwSize;
        dwTotalSize += dwSize;
        SafeRelease(&pBuffer);
    }

    if (SUCCEEDED(hr)) {
        DWORD size = 0;
        hr = pSample->GetTotalLength(&size);
        assert(dwTotalSize == size);
    }

    TraceError(__FILE__, __LINE__, __FUNCTION__, NULL, hr);
    return SUCCEEDED(hr);
}

bool FreeSample(void const *context)
{
    HRESULT hr = S_OK;
    IMFMediaBuffer      *pBuffer = NULL;
    IMFSample           *pSample = (IMFSample *)context;
    DWORD               dwBufferCount = 0;

    hr = pSample->GetBufferCount(&dwBufferCount);

    if (FAILED(hr))
    {
        TraceError(__FILE__, __LINE__, __FUNCTION__, NULL, hr);
        return false;
    }

    for (DWORD i = 0; i < dwBufferCount; ++i)
    {
        hr = pSample->GetBufferByIndex(i, &pBuffer);
        if (FAILED(hr))
        {
            break;
        }
        hr = pBuffer->Unlock();
        if (FAILED(hr))
        {
            SafeRelease(&pBuffer);
            break;
        }
        SafeRelease(&pBuffer);
    }

    SafeRelease(&pSample);

    --LockSampleCount;

    TraceError(__FILE__, __LINE__, __FUNCTION__, NULL, hr);
    return SUCCEEDED(hr);
}
