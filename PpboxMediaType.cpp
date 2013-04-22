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
#include "PpboxMediaType.h"
#include <InitGuid.h>
#include <wmcodecdsp.h>

#include "Windows.h"
#include "Windows.Media.h"

#include "SafeRelease.h"
#include "Trace.h"

#define PPBOX_EXTERN
#include "plugins/ppbox/ppbox.h"

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


//-------------------------------------------------------------------
// CreateVideoMediaType:
// Create a media type from an Ppbox video sequence header.
//-------------------------------------------------------------------

HRESULT CreateVideoMediaType(const PPBOX_StreamInfoEx& info, IMFMediaType **ppType)
{
    HRESULT hr = S_OK;

    IMFMediaType *pType = NULL;

    hr = MFCreateMediaType(&pType);

    if (SUCCEEDED(hr))
    {
        hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    }

    if (SUCCEEDED(hr))
    {
        if (info.sub_type == ppbox_video_avc)
            hr = pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
        else
            hr = pType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_WMV3);
    }

    // Format details.
    if (SUCCEEDED(hr))
    {
        // Frame size

        hr = MFSetAttributeSize(
            pType,
            MF_MT_FRAME_SIZE,
            info.video_format.width,
            info.video_format.height
            );
    }

    if (SUCCEEDED(hr))
    {
        // Frame rate

        hr = MFSetAttributeRatio(
            pType,
            MF_MT_FRAME_RATE,
            info.video_format.frame_rate,
            1
            );
    }

    if (SUCCEEDED(hr))
    {
        // foramt data

        hr = pType->SetBlob(
            MF_MT_USER_DATA,
            info.format_buffer,
            info.format_size
            );
    }

    if (SUCCEEDED(hr))
    {
        *ppType = pType;
        (*ppType)->AddRef();
    }

    SafeRelease(&pType);
    return hr;
}

//-------------------------------------------------------------------
// CreateAudioMediaType:
// Create a media type from an Ppbox audio frame header.
//
// Note: This function fills in an PpboxWAVEFORMAT structure and then
// converts the structure to a Media Foundation media type
// (IMFMediaType). This is somewhat roundabout but it guarantees
// that the type can be converted back to an PpboxWAVEFORMAT by the
// decoder if need be.
//
// The WAVEFORMATEX portion of the PpboxWAVEFORMAT structure is
// converted into attributes on the IMFMediaType object. The rest of
// the struct is stored in the MF_MT_USER_DATA attribute.
//-------------------------------------------------------------------
/*
HRESULT LogMediaType(IMFMediaType *pType);
HRESULT CreateAudioMediaType(const PPBOX_StreamInfoEx& info, IMFMediaType **ppType)
{
HRESULT hr = S_OK;
IMFMediaType *pType = NULL;
DWORD dwSize = sizeof(WAVEFORMATEX) + info.format_size;

WAVEFORMATEX  * wf = (WAVEFORMATEX  *)new BYTE[dwSize];
if (wf == 0) 
return(E_OUTOFMEMORY);
memset(wf, 0, dwSize);

wf->wFormatTag = WAVE_FORMAT_WMAUDIO2;
wf->nChannels = info.audio_format.channel_count;
wf->nSamplesPerSec = info.audio_format.sample_rate;
wf->nAvgBytesPerSec = 3995;
wf->nBlockAlign = 742;
wf->wBitsPerSample = info.audio_format.sample_size;
wf->cbSize = info.format_size;
memcpy(wf + 1, info.format_buffer, info.format_size);

// Use the structure to initialize the Media Foundation media type.
hr = MFCreateMediaType(&pType);
if (SUCCEEDED(hr))
{
hr = MFInitMediaTypeFromWaveFormatEx(pType, wf, dwSize);
}

if (SUCCEEDED(hr))
{
*ppType = pType;
(*ppType)->AddRef();
}

LogMediaType(pType);

SafeRelease(&pType);
return hr;
}
//*/
/*
DEFINE_GUID(MEDIASUBTYPE_RAW_AAC1, 0x000000FF, 0x0000, 0x0010, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71);
HRESULT CreateAudioMediaType(const PPBOX_StreamInfoEx& info, IMFMediaType **ppType)
{
HRESULT hr = S_OK;
IMFMediaType *pType = NULL;

hr = MFCreateMediaType(&pType);

if (SUCCEEDED(hr))
{
hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
}

// Subtype = Ppbox payload
if (SUCCEEDED(hr))
{
hr = pType->SetGUID(MF_MT_SUBTYPE, MEDIASUBTYPE_RAW_AAC1);
}

// Format details.
if (SUCCEEDED(hr))
{
// Sample size

hr = pType->SetUINT32(
MF_MT_AUDIO_BITS_PER_SAMPLE,
info.audio_format.sample_size
);
}
if (SUCCEEDED(hr))
{
// Channel count

hr = pType->SetUINT32(
MF_MT_AUDIO_NUM_CHANNELS,
info.audio_format.channel_count
);
}
if (SUCCEEDED(hr))
{
// Channel count

hr = pType->SetUINT32(
MF_MT_AUDIO_SAMPLES_PER_SECOND,
info.audio_format.sample_rate
);
}
if (SUCCEEDED(hr))
{
// foramt data

hr = pType->SetBlob(
MF_MT_USER_DATA,
info.format_buffer,
info.format_size
);
}
if (SUCCEEDED(hr))
{
*ppType = pType;
(*ppType)->AddRef();
}

SafeRelease(&pType);
return hr;
}
//*/
//*
HRESULT Fill_HEAACWAVEFORMAT(const PPBOX_StreamInfoEx& info, PHEAACWAVEFORMAT format)
{
    PWAVEFORMATEX wf = &format->wfInfo.wfx;
    wf->wFormatTag = WAVE_FORMAT_MPEG_HEAAC;
    wf->nChannels = (WORD)info.audio_format.channel_count;
    wf->nSamplesPerSec = info.audio_format.sample_rate;
    wf->nAvgBytesPerSec = 0;
    wf->nBlockAlign = 1;
    wf->wBitsPerSample = (WORD)info.audio_format.sample_size;
    wf->cbSize = (WORD)(sizeof(format->wfInfo) - sizeof(format->wfInfo.wfx) + info.format_size);
    PHEAACWAVEINFO hawi = &format->wfInfo;
    hawi->wPayloadType = 0; // The stream contains raw_data_block elements only. 
    hawi->wAudioProfileLevelIndication = 0x29;
    hawi->wStructType = 0;
    hawi->wReserved1 = 0;
    hawi->dwReserved2 = 0;
    memcpy(format->pbAudioSpecificConfig, info.format_buffer, info.format_size);
    return S_OK;
}

HRESULT CreateAudioMediaType(const PPBOX_StreamInfoEx& info, IMFMediaType **ppType)
{
    HRESULT hr = S_OK;
    IMFMediaType *pType = NULL;

    hr = MFCreateMediaType(&pType);

    if (SUCCEEDED(hr))
    {
        hr = pType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    }

    // Subtype = Ppbox payload
    if (SUCCEEDED(hr))
    {
        if (info.sub_type == ppbox_audio_aac)
            hr = pType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
        else if (info.sub_type == ppbox_audio_mp3)
            hr = pType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_MP3);
        else
            hr = pType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_WMAudioV8);
    }

    // Format details.
    if (SUCCEEDED(hr))
    {
        // Sample size

        hr = pType->SetUINT32(
            MF_MT_AUDIO_BITS_PER_SAMPLE,
            info.audio_format.sample_size
            );
    }
    if (SUCCEEDED(hr))
    {
        // Channel count

        hr = pType->SetUINT32(
            MF_MT_AUDIO_NUM_CHANNELS,
            info.audio_format.channel_count
            );
    }
    if (SUCCEEDED(hr))
    {
        // Sample rate

        hr = pType->SetUINT32(
            MF_MT_AUDIO_SAMPLES_PER_SECOND,
            info.audio_format.sample_rate
            );
    }

    if (SUCCEEDED(hr))
    {
        // foramt data

        hr = pType->SetBlob(
            MF_MT_USER_DATA,
            info.format_buffer,
            info.format_size
            );
    }

    if (info.sub_type == ppbox_audio_aac)
    {
        if (SUCCEEDED(hr))
        {
            hr = pType->SetUINT32(
                MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION,
                0x29
                );
        }
        if (SUCCEEDED(hr))
        {
            hr = pType->SetUINT32(
                MF_MT_AAC_PAYLOAD_TYPE,
                0
                );
        }
        if (SUCCEEDED(hr))
        {
            // foramt data
            struct {
                HEAACWAVEFORMAT format;
                char aac_config_data_pad[256];
            } format;

            Fill_HEAACWAVEFORMAT(info, &format.format);

            hr = pType->SetBlob(
                MF_MT_USER_DATA,
                (UINT8 const *)&format.format.wfInfo.wPayloadType,
                sizeof(HEAACWAVEINFO) - sizeof(WAVEFORMATEX) + info.format_size
                );
        }
    } // if (info.sub_type == ppbox_audio_aac)

    if (SUCCEEDED(hr))
    {
        *ppType = pType;
        (*ppType)->AddRef();
    }

    SafeRelease(&pType);
    return hr;
}
