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
#include <atlconv.h>

#include "SafeRelease.h"
#include "Trace.h"

#include "PpboxMediaSink.h"
#include "PpboxMediaType.h"
//-------------------------------------------------------------------
//
// Notes:
// This sample contains an Ppbox Sink.
//
// - The Sinkparses Ppbox systems-layer streams and generates
//   samples that contain Ppbox payloads.
// - The Sinkdoes not support files that contain a raw Ppbox
//   video or audio stream.
// - The Sinkdoes not support seeking.
//
//-------------------------------------------------------------------

#pragma warning( push )
#pragma warning( disable : 4355 )  // 'this' used in base member initializer list

class AutoLock
{
private:
    CRITICAL_SECTION *m_pCriticalSection;
public:
	_Acquires_lock_(m_pCriticalSection)
    AutoLock(CRITICAL_SECTION& crit)
    {
        m_pCriticalSection = &crit;
        InitializeCriticalSectionEx(m_pCriticalSection, 1000, 0);
    }

	_Releases_lock_(m_pCriticalSection)
    ~AutoLock()
    {
        LeaveCriticalSection(m_pCriticalSection);
    }
};

/* Public class methods */

PpboxMediaSink::PpboxMediaSink() :
    m_cRef(1),
    m_state(STATE_INVALID),
    m_cRestartCounter(0),
	m_bLive(FALSE),
    m_uDuration(0),
	m_uTime(0)
{
    auto module = ::Microsoft::WRL::GetModuleBase();
    if (module != nullptr)
    {
        module->IncrementObjectCount();
    }
}

PpboxMediaSink::~PpboxMediaSink()
{
    if (m_state != STATE_SHUTDOWN)
    {
        Shutdown();
    }

    auto module = ::Microsoft::WRL::GetModuleBase();
    if (module != nullptr)
    {
        module->DecrementObjectCount();
    }
}


// IMediaExtension methods
IFACEMETHODIMP PpboxMediaSink::SetProperties(ABI::Windows::Foundation::Collections::IPropertySet *pConfiguration)
{
    HRESULT hr = S_OK;
    hr = ConvertConfigurationsToMediaTypes(pConfiguration, &m_MediaTypes);
    if (SUCCEEDED(hr))
    {
        auto pos = m_MediaTypes.FrontPosition();
        for (DWORD i = 0; i < m_MediaTypes.GetCount(); ++i)
        {
            ComPtr<IMFMediaType> spMT;
            ComPtr<IMFStreamSink> spStream;
            hr = m_MediaTypes.GetItemByPosition(pos, &spMT);
            if (FAILED(hr))
            {
                break;
            }
            hr = AddStreamSink(i, spMT.Get(), &spStream);
            if (FAILED(hr))
            {
                break;
            }
            pos = m_MediaTypes.Next(pos);
        }
    }

    if (SUCCEEDED(hr))
    {
        USES_CONVERSION;

        HSTRING dest = NULL;
        GetDestinationtFromConfigurations(pConfiguration, &dest);
        PCWSTR pswDest = WindowsGetStringRawBuffer(dest, NULL);
        LPSTR pszDest = pswDest ? W2A(pswDest) : NULL;
        m_PpboxCapture = PPBOX_CaptureCreate("winrt", pszDest);
        PPBOX_CaptureConfigData config;
        config.stream_count = m_streams.GetCount();
        config.ordered = false;
        config.get_sample_buffers = GetSampleBuffers;
        config.free_sample = FreeSample;
        PPBOX_CaptureInit(m_PpboxCapture, &config);
    }

    TRACEHR_RET(hr);
}

//-------------------------------------------------------------------
// IMFMediaSink methods
//-------------------------------------------------------------------

//-------------------------------------------------------------------
// GetCharacteristics
// Returns capabilities flags.
//-------------------------------------------------------------------

HRESULT PpboxMediaSink::GetCharacteristics(DWORD* pdwCharacteristics)
{
    if (pdwCharacteristics == NULL)
    {
        return E_POINTER;
    }

    HRESULT hr = S_OK;

    AutoLock lock(m_critSec);

    hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
		*pdwCharacteristics = MEDIASINK_RATELESS;
    }

    TRACEHR_RET(hr);
}

HRESULT PpboxMediaSink:: AddStreamSink(DWORD dwStreamSinkIdentifier, IMFMediaType *pMediaType, IMFStreamSink **ppStreamSink)
{
    PpboxStreamSink *pStream = nullptr;
    ComPtr<IMFStreamSink> spMFStream;

    AutoLock lock(m_critSec);

    HRESULT hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        hr = GetStreamSinkById(dwStreamSinkIdentifier, &spMFStream);
    }

    if (SUCCEEDED(hr))
    {
        hr = MF_E_STREAMSINK_EXISTS;
    }
    else
    {
        hr = S_OK;
    }

    if (SUCCEEDED(hr))
    {
        pStream = new PpboxStreamSink(dwStreamSinkIdentifier);
        if (pStream == nullptr)
        {
            hr = E_OUTOFMEMORY;
        }
        spMFStream.Attach(pStream);
    }

    // Initialize the stream.
    if (SUCCEEDED(hr))
    {
        hr = pStream->Initialize(this, pMediaType);
    }

    if (SUCCEEDED(hr))
    {
        hr = m_streams.InsertBack(pStream);
    }

    if (SUCCEEDED(hr))
    {
        *ppStreamSink = spMFStream.Detach();
    }

    TRACEHR_RET(hr);
}

HRESULT PpboxMediaSink:: RemoveStreamSink(DWORD dwStreamSinkIdentifier)
{
    AutoLock lock(m_critSec);

    HRESULT hr = CheckShutdown();

    auto pos = m_streams.FrontPosition();
    auto end = m_streams.EndPosition();
    ComPtr<PpboxStreamSink> spStream;

    if (SUCCEEDED(hr))
    {

        for (; pos != end; pos = m_streams.Next(pos))
        {
            hr = m_streams.GetItemByPosition(pos, &spStream);
            if (FAILED(hr))
            {
                break;
            }

            DWORD dwId;
            hr = spStream->GetIdentifier(&dwId);
            if (FAILED(hr) || dwId == dwStreamSinkIdentifier)
            {
                break;
            }
        }

        if (pos == end)
        {
            hr = MF_E_INVALIDSTREAMNUMBER;
        }
    }

    if (SUCCEEDED(hr))
    {
        hr = m_streams.RemoveItemByPosition(pos, nullptr);
        spStream->Shutdown();
    }

    TRACEHR_RET(hr);
}

HRESULT PpboxMediaSink:: GetStreamSinkCount(DWORD *pcStreamSinkCount)
{
    if (pcStreamSinkCount == NULL)
    {
        return E_POINTER;
    }

    AutoLock lock(m_critSec);

    HRESULT hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        *pcStreamSinkCount = m_streams.GetCount();
    }

    TRACEHR_RET(hr);
}

HRESULT PpboxMediaSink:: GetStreamSinkByIndex(DWORD dwIndex, IMFStreamSink **ppStreamSink)
{
    if (ppStreamSink == NULL)
    {
        return E_POINTER;
    }

    AutoLock lock(m_critSec);

    HRESULT hr = CheckShutdown();

    ComPtr<PpboxStreamSink> spStream;

    if (SUCCEEDED(hr))
    {
        auto pos = m_streams.FrontPosition();
        auto end = m_streams.EndPosition();
        DWORD dwCurrent = 0;

        for (;pos != end && dwCurrent < dwIndex; pos = m_streams.Next(pos), ++dwCurrent)
        {
            // Just move to proper position
        }

        if (pos == end)
        {
            hr = MF_E_UNEXPECTED;
        }
        else
        {
            hr = m_streams.GetItemByPosition(pos, &spStream);
        }
    }

    if (SUCCEEDED(hr))
    {
        *ppStreamSink = spStream.Detach();
    }

    TRACEHR_RET(hr);
}

HRESULT PpboxMediaSink:: GetStreamSinkById(DWORD dwStreamSinkIdentifier, IMFStreamSink **ppStreamSink)
{
    if (ppStreamSink == NULL)
    {
        return E_INVALIDARG;
    }

    AutoLock lock(m_critSec);

    HRESULT hr = CheckShutdown();
    ComPtr<PpboxStreamSink> spResult;

    if (SUCCEEDED(hr))
    {
        auto pos = m_streams.FrontPosition();
        auto end = m_streams.EndPosition();

        for (;pos != end; pos = m_streams.Next(pos))
        {
            ComPtr<PpboxStreamSink> spStream;
            hr = m_streams.GetItemByPosition(pos, &spStream);
            if (FAILED(hr))
            {
                break;
            }

            DWORD dwId;
            hr = spStream->GetIdentifier(&dwId);
            if (FAILED(hr))
            {
                break;
            }
            else if (dwId == dwStreamSinkIdentifier)
            {
                spResult = spStream;
                break;
            }
        }

        if (pos == end)
        {
            hr = MF_E_INVALIDSTREAMNUMBER;
        }
    }

    if (SUCCEEDED(hr))
    {
        assert(spResult);
        *ppStreamSink = spResult.Detach();
    }

    TRACEHR_RET(hr);
}

HRESULT PpboxMediaSink:: SetPresentationClock(IMFPresentationClock *pPresentationClock)
{
    AutoLock lock(m_critSec);

    HRESULT hr = CheckShutdown();

    // If we already have a clock, remove ourselves from that clock's
    // state notifications.
    if (SUCCEEDED(hr))
    {
        if (m_spClock)
        {
            hr = m_spClock->RemoveClockStateSink(this);
        }
    }

    // Register ourselves to get state notifications from the new clock.
    if (SUCCEEDED(hr))
    {
        if (pPresentationClock)
        {
            hr = pPresentationClock->AddClockStateSink(this);
        }
    }

    if (SUCCEEDED(hr))
    {
        // Release the pointer to the old clock.
        // Store the pointer to the new clock.
        m_spClock = pPresentationClock;
    }

    TRACEHR_RET(hr);
}

HRESULT PpboxMediaSink:: GetPresentationClock(IMFPresentationClock **ppPresentationClock)
{
    if (ppPresentationClock == NULL)
    {
        return E_INVALIDARG;
    }

    AutoLock lock(m_critSec);

    HRESULT hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        if (m_spClock == NULL)
        {
            hr = MF_E_NO_CLOCK; // There is no presentation clock.
        }
        else
        {
            // Return the pointer to the caller.
            *ppPresentationClock = m_spClock.Get();
            (*ppPresentationClock)->AddRef();
        }
    }

    TRACEHR_RET(hr);
}


//-------------------------------------------------------------------
// Shutdown
// Shuts down the Sinkand releases all reSinks.
//-------------------------------------------------------------------

HRESULT PpboxMediaSink::Shutdown()
{
    EnterCriticalSection(&m_critSec);

    HRESULT hr = S_OK;

    PpboxStreamSink *pStream = NULL;

    hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        // Shut down the stream objects.
        // Set the state.
        m_state = STATE_SHUTDOWN;
    }

    LeaveCriticalSection(&m_critSec);
    TRACEHR_RET(hr);
}

//-------------------------------------------------------------------
// IMFMediaSink methods
//-------------------------------------------------------------------

HRESULT PpboxMediaSink:: OnClockStart(MFTIME hnsSystemTime, LONGLONG llClockStartOffset)
{
    AutoLock lock(m_critSec);

    HRESULT hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        TRACE(TRACE_LEVEL_LOW, L"OnClockStart ts=%I64d\n", llClockStartOffset);
        // Start each stream.
        //_llStartTime = llClockStartOffset;
        hr = ForEach(m_streams, [llClockStartOffset](PpboxStreamSink * pStream){
            return pStream->Start(llClockStartOffset);
        });
    }

    TRACEHR_RET(hr);
}


HRESULT PpboxMediaSink:: OnClockStop(MFTIME hnsSystemTime)
{
    AutoLock lock(m_critSec);

    HRESULT hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        // Stop each stream
        hr = ForEach(m_streams, [](PpboxStreamSink * pStream){
            return pStream->Stop();
        });
    }

    TRACEHR_RET(hr);
}


HRESULT PpboxMediaSink:: OnClockPause(MFTIME hnsSystemTime)
{
    return MF_E_INVALID_STATE_TRANSITION;
}


HRESULT PpboxMediaSink:: OnClockRestart(MFTIME hnsSystemTime)
{
    return MF_E_INVALID_STATE_TRANSITION;
}


HRESULT PpboxMediaSink:: OnClockSetRate(MFTIME hnsSystemTime, float flRate)
{
    return S_OK;
}


//-------------------------------------------------------------------
// Public non-interface methods
//-------------------------------------------------------------------

/* Private methods */

//-------------------------------------------------------------------
// IsInitialized:
// Returns S_OK if the Sinkis correctly initialized with an
// Ppbox byte stream. Otherwise, returns MF_E_NOT_INITIALIZED.
//-------------------------------------------------------------------

HRESULT PpboxMediaSink::IsInitialized() const
{
    if (m_state == STATE_INVALID)
    {
        return MF_E_NOT_INITIALIZED;
    }
    else
    {
        return S_OK;
    }
}


#pragma warning( pop )
