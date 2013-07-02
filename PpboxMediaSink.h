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


#pragma once

#include <new.h>
#include <windows.h>
#include <assert.h>

#ifndef _ASSERTE
#define _ASSERTE assert
#endif

#include <wrl\client.h>
#include <wrl\implements.h>
#include <wrl\ftm.h>
#include <wrl\event.h>
#include <wrl\wrappers\corewrappers.h>

#include "Windows.Media.h"

#include <mfapi.h>
#include <mfobjects.h>
#include <mfidl.h>
#include <mferror.h>

#pragma comment(lib, "mfplat")
#pragma comment(lib, "mfuuid")      // Media Foundation GUIDs
// Forward declares
class PpboxMediaSink;
class PpboxStreamSink;

#include "ComPtrList.h"

enum SinkState
{
    STATE_INVALID,      // Initial state. Have not started opening the stream.
    STATE_STOPPED,
    STATE_PAUSED,
    STATE_STARTED,
    STATE_SHUTDOWN
};

#include "PpboxStreamSink.h"    // Ppbox stream

#include <vector>

const UINT32 MAX_STREAMS = 32;


// Constants

const DWORD INITIAL_BUFFER_SIZE = 4 * 1024; // Initial size of the read buffer. (The buffer expands dynamically.)
const DWORD READ_SIZE = 4 * 1024;           // Size of each read request.
const DWORD SAMPLE_QUEUE = 2;               // How many samples does each stream try to hold in its queue?

#ifndef RUNTIMECLASS_GeometricSource_GeometricSchemeHandler_DEFINED
#define RUNTIMECLASS_GeometricSource_GeometricSchemeHandler_DEFINED
extern const __declspec(selectany) WCHAR RuntimeClass_PpboxSink_PpboxMediaSink[] = L"PpboxSink.PpboxMediaSink";
#endif

// PpboxMediaSink: The media Sink object.
class PpboxMediaSink
    : public Microsoft::WRL::RuntimeClass<
		Microsoft::WRL::RuntimeClassFlags< Microsoft::WRL::RuntimeClassType::WinRtClassicComMix >, 
		ABI::Windows::Media::IMediaExtension,
        IMFClockStateSink, 
		IMFMediaSink >
{
    InspectableClass(RuntimeClass_PpboxSink_PpboxMediaSink, BaseTrust)
public:
    PpboxMediaSink();

    ~PpboxMediaSink();

    IFACEMETHOD (SetProperties) (ABI::Windows::Foundation::Collections::IPropertySet *pConfiguration);

public:
    PP_handle GetPpboxCapture()
    {
        return m_PpboxCapture;
    }

public:
    // IMFMediaSink
    STDMETHODIMP GetCharacteristics(DWORD* pdwCharacteristics);
    STDMETHODIMP AddStreamSink(DWORD dwStreamSinkIdentifier, IMFMediaType *pMediaType, IMFStreamSink **ppStreamSink);
    STDMETHODIMP RemoveStreamSink(DWORD dwStreamSinkIdentifier);
    STDMETHODIMP GetStreamSinkCount(DWORD *pcStreamSinkCount);
    STDMETHODIMP GetStreamSinkByIndex(DWORD dwIndex, IMFStreamSink **ppStreamSink);
    STDMETHODIMP GetStreamSinkById(DWORD dwStreamSinkIdentifier, IMFStreamSink **ppStreamSink);
    STDMETHODIMP SetPresentationClock(IMFPresentationClock *pPresentationClock);
    STDMETHODIMP GetPresentationClock(IMFPresentationClock **ppPresentationClock);
    STDMETHODIMP Shutdown();

    // IMFClockStateSink 
    STDMETHODIMP OnClockStart(MFTIME hnsSystemTime, LONGLONG llClockStartOffset);
    STDMETHODIMP OnClockStop(MFTIME hnsSystemTime);
    STDMETHODIMP OnClockPause(MFTIME hnsSystemTime);
    STDMETHODIMP OnClockRestart(MFTIME hnsSystemTime);
    STDMETHODIMP OnClockSetRate(MFTIME hnsSystemTime, float flRate);


    // (This method is public because the streams call it.)
    HRESULT EndOfStream();

    HRESULT RequestSample();

    // Lock/Unlock:
    // Holds and releases the Sink's critical section. Called by the streams.
    void    Lock() { EnterCriticalSection(&m_critSec); }
    void    Unlock() { LeaveCriticalSection(&m_critSec); }

private:
    // CheckShutdown: Returns MF_E_SHUTDOWN if the Sinkwas shut down.
    HRESULT CheckShutdown() const
    {
        return ( m_state == STATE_SHUTDOWN ? MF_E_SHUTDOWN : S_OK );
    }

    HRESULT     CompleteOpen(HRESULT hrStatus);

    HRESULT     IsInitialized() const;

private:
    long                        m_cRef;                     // reference count

    CRITICAL_SECTION            m_critSec;                  // critical section for thread safety
    SinkState                   m_state;                    // Current state (running, stopped, paused)

    ComPtrList<IMFMediaType>    m_MediaTypes;

    ComPtrList<PpboxStreamSink> m_streams;                  // Array of streams.
    ComPtr<IMFPresentationClock>m_spClock;                   // Presentation clock.

    DWORD                       m_cPendingEOS;              // Pending EOS notifications.

    BOOL                        m_bLive;
    UINT64                      m_uDuration;
    UINT64                      m_uTime;

    PP_handle                   m_PpboxCapture;
};


