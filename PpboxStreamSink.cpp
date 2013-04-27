//////////////////////////////////////////////////////////////////////////
//
// PpboxStreamSink.cpp
// Implements the stream object (IMFStreamSink) for the Ppbox Sink.
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
#include "plugins/ppbox/ppbox.h"
#include "PpboxMediaSink.h"
#include "PpboxMediaType.h"
#include "SafeRelease.h"
#include "Trace.h"

#include <Mfidl.h>

#pragma warning( push )
#pragma warning( disable : 4355 )  // 'this' used in base member initializer list


/* PpboxStreamSink::SinkLock class methods */

//-------------------------------------------------------------------
// PpboxStreamSink::SinkLock constructor - locks the Sink
//-------------------------------------------------------------------

PpboxStreamSink::SinkLock::SinkLock(ComPtr<PpboxMediaSink> &pSink)
    : m_pSink(NULL)
{
    if (pSink)
    {
        m_pSink = pSink.Get();
        m_pSink->AddRef();
        m_pSink->Lock();
    }
}

//-------------------------------------------------------------------
// PpboxStreamSink::SinkLock destructor - unlocks the Sink
//-------------------------------------------------------------------

PpboxStreamSink::SinkLock::~SinkLock()
{
    if (m_pSink)
    {
        m_pSink->Unlock();
        m_pSink->Release();
    }
}



//-------------------------------------------------------------------
// Public non-interface methods
//-------------------------------------------------------------------


PpboxStreamSink::PpboxStreamSink(DWORD dwIdentifier) :
    m_cRef(1),
    m_dwIdentifier(dwIdentifier),
    m_pEventQueue(NULL),
    m_state(State_TypeNotSet),
    m_IsShutdown(FALSE),
    m_bActive(FALSE),
    m_bEOS(FALSE)
{
    //assert(pSD != NULL);

    auto module = ::Microsoft::WRL::GetModuleBase();
    if (module != nullptr)
    {
        module->IncrementObjectCount();
    }
}

PpboxStreamSink::~PpboxStreamSink()
{
    assert(m_state == STATE_SHUTDOWN);
    m_pSink.Reset();

    auto module = ::Microsoft::WRL::GetModuleBase();
    if (module != nullptr)
    {
        module->DecrementObjectCount();
    }
}


HRESULT PpboxStreamSink::Initialize(PpboxMediaSink *pParent, IMFMediaType *pMediaType)
{
    assert(pParent != NULL);

    HRESULT hr = S_OK;

	m_pSink = pParent;

    SinkLock lock(m_pSink);

    // Create the media event queue.
    hr = MFCreateEventQueue(&m_pEventQueue);

    if (SUCCEEDED(hr) && pMediaType != nullptr)
    {
        m_pMediaType = pMediaType;
        hr = m_pMediaType->GetMajorType(&m_guiType);
        if (SUCCEEDED(hr))
        {
            hr = m_pMediaType->GetGUID(MF_MT_SUBTYPE, &m_guiSubtype);
        }
    }

    TRACEHR_RET(hr);
}


HRESULT PpboxStreamSink::Start(MFTIME start)
{
    SinkLock lock(m_pSink);

    HRESULT hr = S_OK;

    hr = ValidateOperation(OpStart);

    if (SUCCEEDED(hr))
    {
        if (start != PRESENTATION_CURRENT_POSITION)
        {
            m_StartTime = start;        // Cache the start time.
            m_fGetStartTimeFromSample = false;
        }
        else
        {
            m_fGetStartTimeFromSample = true;
        }
        m_state = State_Started;
        //_fWaitingForFirstSample = _fIsVideo;

        // Send MEStreamSinkStarted.
        hr = QueueEvent(MEStreamSinkStarted, GUID_NULL, hr, NULL);

        // There might be samples queue from earlier (ie, while paused).
        if (SUCCEEDED(hr))
        {
            hr = QueueEvent(MEStreamSinkRequestSample, GUID_NULL, hr, NULL);
        }
    }

    TRACEHR_RET(hr);
}

// Called when the presentation clock stops.
HRESULT PpboxStreamSink::Stop()
{
    SinkLock lock(m_pSink);

    HRESULT hr = S_OK;

    hr = ValidateOperation(OpStop);

    if (SUCCEEDED(hr))
    {
        m_state = State_Stopped;
        hr = QueueEvent(MEStreamSinkStopped, GUID_NULL, hr, NULL);
    }

    TRACEHR_RET(hr);
}

// Called when the presentation clock pauses.
HRESULT PpboxStreamSink::Pause()
{
    SinkLock lock(m_pSink);

    HRESULT hr = S_OK;

    hr = ValidateOperation(OpPause);

    if (SUCCEEDED(hr))
    {
        m_state = State_Paused;
        hr = QueueEvent(MEStreamSinkPaused, GUID_NULL, hr, NULL);
    }

    TRACEHR_RET(hr);
}

// Called when the presentation clock restarts.
HRESULT PpboxStreamSink::Restart()
{
    SinkLock lock(m_pSink);

    HRESULT hr = S_OK;

    hr = ValidateOperation(OpRestart);

    if (SUCCEEDED(hr))
    {
        m_state = State_Started;

        // Send MEStreamSinkStarted.
        hr = QueueEvent(MEStreamSinkStarted, GUID_NULL, hr, NULL);

        // There might be samples queue from earlier (ie, while paused).
        if (SUCCEEDED(hr))
        {
            hr = QueueEvent(MEStreamSinkRequestSample, GUID_NULL, hr, NULL);
        }
    }

    TRACEHR_RET(hr);
}

//-------------------------------------------------------------------
// Shutdown
// Shuts down the stream and releases all reSinks.
//-------------------------------------------------------------------

HRESULT PpboxStreamSink::Shutdown()
{
    SinkLock lock(m_pSink);

    HRESULT hr = S_OK;

    hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        m_IsShutdown = TRUE;

        // Shut down the event queue.
        if (m_pEventQueue)
        {
            m_pEventQueue->Shutdown();
        }

        // Release objects.
        m_pMediaType.Reset();
        m_pEventQueue.Reset();
        //m_pSink.Reset();

        // NOTE:
        // Do NOT release the Sink pointer here, because the stream uses
        // it to hold the critical section. In particular, the stream must
        // hold the critical section when checking the shutdown status,
        // which obviously can occur after the stream is shut down.

        // It is OK to hold a ref count on the Sink after shutdown,
        // because the Sink releases its ref count(s) on the streams,
        // which breaks the circular ref count.
    }

    TRACEHR_RET(hr);
}


/* Public class methods */

//-------------------------------------------------------------------
// IUnknown methods
//-------------------------------------------------------------------

ULONG PpboxStreamSink::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

ULONG PpboxStreamSink::Release()
{
    LONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0)
    {
        delete this;
    }
    return cRef;
}

HRESULT PpboxStreamSink::QueryInterface(REFIID riid, void** ppv)
{
    if (ppv == nullptr)
    {
        return E_POINTER;
    }
    HRESULT hr = E_NOINTERFACE;
    (*ppv) = nullptr;
    if (riid == IID_IUnknown || 
        riid == IID_IMFMediaEventGenerator ||
        riid == IID_IMFStreamSink)
    {
        (*ppv) = static_cast<IMFStreamSink *>(this);
        AddRef();
        hr = S_OK;
    }

    TRACEHR_RET(hr);
}


//-------------------------------------------------------------------
// IMFMediaEventGenerator methods
//
// For remarks, see PpboxMediaSink.cpp
//-------------------------------------------------------------------

HRESULT PpboxStreamSink::BeginGetEvent(IMFAsyncCallback* pCallback,IUnknown* punkState)
{
    HRESULT hr = S_OK;

    SinkLock lock(m_pSink);

    hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        hr = m_pEventQueue->BeginGetEvent(pCallback, punkState);
    }

    TRACEHR_RET(hr);
}

HRESULT PpboxStreamSink::EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent)
{
    HRESULT hr = S_OK;

    SinkLock lock(m_pSink);

    hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        hr = m_pEventQueue->EndGetEvent(pResult, ppEvent);
    }

    TRACEHR_RET(hr);
}

HRESULT PpboxStreamSink::GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent)
{
    HRESULT hr = S_OK;

    ComPtr<IMFMediaEventQueue> pQueue;

    { // scope for lock

        SinkLock lock(m_pSink);

        // Check shutdown
        hr = CheckShutdown();

        // Cache a local pointer to the queue.
        if (SUCCEEDED(hr))
        {
            pQueue = m_pEventQueue;
        }
    }   // release lock

    // Use the local pointer to call GetEvent.
    if (SUCCEEDED(hr))
    {
        hr = pQueue->GetEvent(dwFlags, ppEvent);
    }

    TRACEHR_RET(hr);
}

HRESULT PpboxStreamSink::QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue)
{
    HRESULT hr = S_OK;

    SinkLock lock(m_pSink);

    hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        hr = m_pEventQueue->QueueEventParamVar(met, guidExtendedType, hrStatus, pvValue);
    }

    TRACEHR_RET(hr);
}

//-------------------------------------------------------------------
// IMFStreamSink methods
//-------------------------------------------------------------------


//-------------------------------------------------------------------
// GetMediaSink:
// Returns a pointer to the media Sink.
//-------------------------------------------------------------------

HRESULT PpboxStreamSink::GetMediaSink(IMFMediaSink** ppMediaSink)
{
    SinkLock lock(m_pSink);

    if (ppMediaSink == NULL)
    {
        return E_POINTER;
    }

    if (m_pSink == NULL)
    {
        return E_UNEXPECTED;
    }

    HRESULT hr = S_OK;

    hr = CheckShutdown();

    // QI the Sink for IMFMediaSink.
    // (Does not hold the Sink's critical section.)
    if (SUCCEEDED(hr))
    {
        ComPtr<IMFMediaSink> spSink = m_pSink;
        *ppMediaSink = spSink.Detach();
    }
    TRACEHR_RET(hr);
}


//-------------------------------------------------------------------
// GetStreamDescriptor:
// Returns a pointer to the stream descriptor for this stream.
//-------------------------------------------------------------------

HRESULT PpboxStreamSink::GetIdentifier(DWORD *pdwIdentifier)
{
    if (pdwIdentifier == NULL)
    {
        return E_INVALIDARG;
    }

    SinkLock lock(m_pSink);

    HRESULT hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        *pdwIdentifier = m_dwIdentifier;
    }

    TRACEHR_RET(hr);
}


HRESULT PpboxStreamSink::GetMediaTypeHandler(IMFMediaTypeHandler **ppHandler)
{
    if (ppHandler == NULL)
    {
        return E_INVALIDARG;
    }

    SinkLock lock(m_pSink);

    HRESULT hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        ComPtr<IMFMediaTypeHandler> spTypeHandler = this;
        *ppHandler = spTypeHandler.Detach();
    }

    TRACEHR_RET(hr);
}


HRESULT PpboxStreamSink::ProcessSample(IMFSample *pSample)
{
    if (pSample == NULL)
    {
        return E_INVALIDARG;
    }

    HRESULT hr = S_OK;

    SinkLock lock(m_pSink);

    hr = CheckShutdown();

    // Validate the operation.
    if (SUCCEEDED(hr))
    {
        hr = ValidateOperation(OpProcessSample);
    }

    if (SUCCEEDED(hr))
    {
        if (m_dwIdentifier == 1)
        {
            //TRACE(0, L"PpboxStreamSink::ProcessSample id = %u\r\n", m_dwIdentifier);
            //PrintSampleInfo(pSample);
        }

        PPBOX_Sample sample;
        CreateSample(sample, pSample);
        sample.itrack = m_dwIdentifier;
        PPBOX_CapturePutSample(m_pSink->GetPpboxCapture(), &sample);
    }

    if (SUCCEEDED(hr))
    {
        hr = QueueEvent(MEStreamSinkRequestSample, GUID_NULL, hr, NULL);
    }

    TRACEHR_RET(hr);
}


HRESULT PpboxStreamSink::PlaceMarker(MFSTREAMSINK_MARKER_TYPE eMarkerType, const PROPVARIANT *pvarMarkerValue, const PROPVARIANT *pvarContextValue)
{
    SinkLock lock(m_pSink);

    HRESULT hr = S_OK;

    hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        hr = ValidateOperation(OpPlaceMarker);
    }

    // Unless we are paused, start an async operation to dispatch the next sample/marker.
    if (SUCCEEDED(hr))
    {
        if (eMarkerType == MFSTREAMSINK_MARKER_ENDOFSEGMENT)
        {
            m_bEOS = true;
        }

        if (m_state != State_Paused)
        {
            hr = QueueEvent(MEStreamSinkMarker, GUID_NULL, S_OK, pvarContextValue);
        }
    }

    TRACEHR_RET(hr);
}


HRESULT PpboxStreamSink::Flush(void)
{
    SinkLock lock(m_pSink);

    HRESULT hr = CheckShutdown();

    // Nothing To do

    TRACEHR_RET(hr);
}



/// IMFMediaTypeHandler methods

// Check if a media type is supported.
IFACEMETHODIMP PpboxStreamSink::IsMediaTypeSupported(
    /* [in] */ IMFMediaType *pMediaType,
    /* [out] */ IMFMediaType **ppMediaType)
{
    if (pMediaType == nullptr)
    {
        return E_INVALIDARG;
    }

    Trace(0, L"[PpboxStreamSink::IsMediaTypeSupported] id = %u\r\n", m_dwIdentifier);

    PrintMediaType(pMediaType);

    SinkLock lock(m_pSink);

    GUID majorType = GUID_NULL;
    UINT cbSize = 0;

    HRESULT hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        hr = pMediaType->GetGUID(MF_MT_MAJOR_TYPE, &majorType);
    }

    // First make sure it's video or audio type.
    if (SUCCEEDED(hr))
    {
        if (majorType != MFMediaType_Video && majorType != MFMediaType_Audio)
        {
            hr = MF_E_INVALIDTYPE;
        }
    }

    if (SUCCEEDED(hr) && m_pMediaType != nullptr)
    {
        GUID guiNewSubtype;
        if (FAILED(pMediaType->GetGUID(MF_MT_SUBTYPE, &guiNewSubtype)) || 
            guiNewSubtype != m_guiSubtype)
        {
            hr = MF_E_INVALIDTYPE;
        }
    }

    // We don't return any "close match" types.
    if (ppMediaType)
    {
        //ComPtr<IMFMediaType> spMediaType = m_pMediaType;
        //*ppMediaType = spMediaType.Detach();
    }

    TRACEHR_RET(hr);
}


// Return the number of preferred media types.
IFACEMETHODIMP PpboxStreamSink::GetMediaTypeCount(DWORD *pdwTypeCount)
{
    if (pdwTypeCount == nullptr)
    {
        return E_INVALIDARG;
    }

    SinkLock lock(m_pSink);

    HRESULT hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        // We've got only one media type
        *pdwTypeCount = 1;
    }

    TRACEHR_RET(hr);
}


// Return a preferred media type by index.
IFACEMETHODIMP PpboxStreamSink::GetMediaTypeByIndex(
    /* [in] */ DWORD dwIndex,
    /* [out] */ IMFMediaType **ppType)
{
    if (ppType == NULL)
    {
        return E_INVALIDARG;
    }

    SinkLock lock(m_pSink);

    HRESULT hr = CheckShutdown();

    if ( dwIndex > 0 )
    {
        hr = MF_E_NO_MORE_TYPES;
    }
    else
    {
        *ppType = m_pMediaType.Get();
        if (*ppType != nullptr)
        {
            (*ppType)->AddRef();
        }
    }

    TRACEHR_RET(hr);
}

// Set the current media type.
IFACEMETHODIMP PpboxStreamSink::SetCurrentMediaType(IMFMediaType *pMediaType)
{
    if (pMediaType == NULL)
    {
        return E_INVALIDARG;
    }

    SinkLock lock(m_pSink);

    HRESULT hr = CheckShutdown();

    // We don't allow format changes after streaming starts.
    if (SUCCEEDED(hr))
    {
        hr = ValidateOperation(OpSetMediaType);
    }

    // We set media type already
    if (m_state >= State_Ready)
    {
        if (SUCCEEDED(hr))
        {
            hr = IsMediaTypeSupported(pMediaType, NULL);
        }
    }

    if (SUCCEEDED(hr))
    {
        hr = IsMediaTypeSupported(pMediaType, NULL);
    }

    if (SUCCEEDED(hr))
    {
        GUID guiMajorType;
        pMediaType->GetMajorType(&guiMajorType);

        m_pMediaType.ReleaseAndGetAddressOf();
        hr = MFCreateMediaType(&m_pMediaType);
        if (SUCCEEDED(hr))
        {
            hr = pMediaType->CopyAllItems(m_pMediaType.Get());
        }
        if (SUCCEEDED(hr))
        {
            hr = m_pMediaType->GetGUID(MF_MT_SUBTYPE, &m_guiSubtype);
        }
        if (SUCCEEDED(hr))
        {
            if (m_state == State_TypeNotSet)
                m_state = State_Ready;

            PPBOX_StreamInfo stream;
            CreateMediaType(stream, m_pMediaType.Get());
            PPBOX_CaptureSetStream(m_pSink->GetPpboxCapture(), m_dwIdentifier, &stream);
        }
    }

    TRACEHR_RET(hr);
}

// Return the current media type, if any.
IFACEMETHODIMP PpboxStreamSink::GetCurrentMediaType(IMFMediaType **ppMediaType)
{
    if (ppMediaType == NULL)
    {
        return E_INVALIDARG;
    }

    SinkLock lock(m_pSink);

    HRESULT hr = CheckShutdown();

    if (SUCCEEDED(hr))
    {
        if (m_pMediaType == nullptr)
        {
            hr = MF_E_NOT_INITIALIZED;
        }
    }

    if (SUCCEEDED(hr))
    {
        *ppMediaType = m_pMediaType.Get();
        (*ppMediaType)->AddRef();
    }

    TRACEHR_RET(hr);
}


// Return the major type GUID.
IFACEMETHODIMP PpboxStreamSink::GetMajorType(GUID *pguidMajorType)
{
    if (pguidMajorType == nullptr)
    {
        return E_INVALIDARG;
    }

    if (!m_pMediaType)
    {
        return MF_E_NOT_INITIALIZED;
    }
    
    *pguidMajorType = m_guiType;

    return S_OK;
}


/* Private methods */

BOOL PpboxStreamSink::ValidStateMatrix[PpboxStreamSink::State_Count][PpboxStreamSink::Op_Count] =
{
// States:    Operations:
//            SetType   Start     Restart   Pause     Stop      Sample    Marker   
/* NotSet */  TRUE,     FALSE,    FALSE,    FALSE,    FALSE,    FALSE,    FALSE,   

/* Ready */   TRUE,     TRUE,     FALSE,    TRUE,     TRUE,     FALSE,    TRUE,    

/* Start */   TRUE,    TRUE,     FALSE,    TRUE,     TRUE,     TRUE,     TRUE,    

/* Pause */   FALSE,    TRUE,     TRUE,     TRUE,     TRUE,     TRUE,     TRUE,    

/* Stop */    FALSE,    TRUE,     FALSE,    FALSE,    TRUE,     FALSE,    TRUE,    

};

// Checks if an operation is valid in the current state.
HRESULT PpboxStreamSink::ValidateOperation(StreamOperation op)
{
    assert(!m_IsShutdown);

    HRESULT hr = S_OK;

    if (ValidStateMatrix[m_state][op])
    {
        return S_OK;
    }
    else if (m_state == State_TypeNotSet)
    {
        TRACEHR_RET (MF_E_NOT_INITIALIZED);
    }
    else
    {
        TRACEHR_RET (MF_E_INVALIDREQUEST);
    }
}

#pragma warning( pop )
