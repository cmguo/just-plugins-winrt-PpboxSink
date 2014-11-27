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

#include <windows.h>
#include <mfapi.h>

#include "Windows.Media.MediaProperties.h"

#include "ComPtrList.h"

HRESULT ConvertPropertiesToMediaType(
    _In_ ABI::Windows::Media::MediaProperties::IMediaEncodingProperties *pMEP, 
    _Outptr_ IMFMediaType **ppMT);

HRESULT ConvertConfigurationsToMediaTypes(
    ABI::Windows::Foundation::Collections::IPropertySet *pConfigurations, 
    ComPtrList<IMFMediaType> * pListMT);

HRESULT GetDestinationtFromConfigurations(
    ABI::Windows::Foundation::Collections::IPropertySet *pConfigurations, 
    HSTRING * pDestinationt);

HRESULT CreateVideoMediaType(JUST_StreamInfo& info, IMFMediaType *pType);
HRESULT CreateAudioMediaType(JUST_StreamInfo& info, IMFMediaType *pType);
HRESULT CreateMediaType(JUST_StreamInfo& info, IMFMediaType *pType);

HRESULT CreateSample(JUST_Sample& sample, IMFSample *pSample);

bool GetSampleBuffers(void const *context, JUST_ConstBuffer * buffers);
bool FreeSample(void const *context);
