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

#include "ComPtrList.h";

HRESULT ConvertPropertiesToMediaType(
    _In_ ABI::Windows::Media::MediaProperties::IMediaEncodingProperties *pMEP, 
    _Outptr_ IMFMediaType **ppMT);

HRESULT ConvertConfigurationsToMediaTypes(
    ABI::Windows::Foundation::Collections::IPropertySet *pConfigurations, 
    ComPtrList<IMFMediaType> * pListMT);

struct PPBOX_StreamInfoEx;

HRESULT CreateVideoMediaType(const PPBOX_StreamInfoEx& info, IMFMediaType **ppType);
HRESULT CreateAudioMediaType(const PPBOX_StreamInfoEx& info, IMFMediaType **ppType);
