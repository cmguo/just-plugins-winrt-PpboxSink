//////////////////////////////////////////////////////////////////////////
//
// PpboxStreamSink.h
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

#pragma once

class PpboxMediaSink;


typedef ComPtrList<IMFSample>       SampleList;
typedef ComPtrList<IUnknown, true>  TokenList;    // List of tokens for IMFMediaStream::RequestSample

// The media stream object.
class PpboxStreamSink : public IMFStreamSink, public IMFMediaTypeHandler
{
public:
    // State enum: Defines the current state of the stream.
    enum State
    {
        State_TypeNotSet = 0,    // No media type is set
        State_Ready,             // Media type is set, Start has never been called.
        State_Started,
        State_Stopped,
        State_Paused,
        State_Count              // Number of states
    };

    // StreamOperation: Defines various operations that can be performed on the stream.
    enum StreamOperation
    {
        OpSetMediaType = 0,
        OpStart,
        OpRestart,
        OpPause,
        OpStop,
        OpProcessSample,
        OpPlaceMarker,

        Op_Count                // Number of operations
    };

public:
    PpboxStreamSink(DWORD dwIdentifier);
    ~PpboxStreamSink();

public:
    HRESULT Initialize(PpboxMediaSink *pParent, IMFMediaType *pMediaType);
    HRESULT Start(MFTIME start);
    HRESULT Restart();
    HRESULT Stop();
    HRESULT Pause();
    HRESULT Shutdown();

public:
    // IUnknown
    STDMETHODIMP QueryInterface(REFIID iid, void** ppv);
    STDMETHODIMP_(ULONG) AddRef();
    STDMETHODIMP_(ULONG) Release();

    // IMFMediaEventGenerator
    STDMETHODIMP BeginGetEvent(IMFAsyncCallback* pCallback,IUnknown* punkState);
    STDMETHODIMP EndGetEvent(IMFAsyncResult* pResult, IMFMediaEvent** ppEvent);
    STDMETHODIMP GetEvent(DWORD dwFlags, IMFMediaEvent** ppEvent);
    STDMETHODIMP QueueEvent(MediaEventType met, REFGUID guidExtendedType, HRESULT hrStatus, const PROPVARIANT* pvValue);

    // IMFStreamSink
    STDMETHODIMP GetMediaSink(IMFMediaSink **ppMediaSink);
    STDMETHODIMP GetIdentifier(DWORD *pdwIdentifier);
    STDMETHODIMP GetMediaTypeHandler(IMFMediaTypeHandler **ppHandler);
    STDMETHODIMP ProcessSample(IMFSample *pSample);
    STDMETHODIMP PlaceMarker(MFSTREAMSINK_MARKER_TYPE eMarkerType, const PROPVARIANT *pvarMarkerValue, const PROPVARIANT *pvarContextValue);
    STDMETHODIMP Flush(void);

    // IMFMediaTypeHandler
    IFACEMETHOD (IsMediaTypeSupported) (IMFMediaType *pMediaType, IMFMediaType **ppMediaType);
    IFACEMETHOD (GetMediaTypeCount) (DWORD *pdwTypeCount);
    IFACEMETHOD (GetMediaTypeByIndex) (DWORD dwIndex, IMFMediaType **ppType);
    IFACEMETHOD (SetCurrentMediaType) (IMFMediaType *pMediaType);
    IFACEMETHOD (GetCurrentMediaType) (IMFMediaType **ppMediaType);
    IFACEMETHOD (GetMajorType) (GUID *pguidMajorType);

    BOOL        IsActive() const { return m_bActive; }
    BOOL        NeedsData();

    HRESULT     DeliverPayload(IMFSample *pSample);

    // Callbacks
    HRESULT     OnDispatchSamples(IMFAsyncResult *pResult);

private:
    static BOOL ValidStateMatrix[State_Count][Op_Count];

    HRESULT     ValidateOperation(StreamOperation op);

private:
    HRESULT     PrepareSample(IMFSample *pSample);

private:

    // SinkLock class:
    // Small helper class to lock and unlock the Sink.
    class SinkLock
    {
    private:
        PpboxMediaSink *m_pSink;
    public:
        SinkLock(ComPtr<PpboxMediaSink> &pSink);
        ~SinkLock();
    };

private:

    HRESULT CheckShutdown() const
    {
        return ( m_IsShutdown ? MF_E_SHUTDOWN : S_OK );
    }


private:
    long                            m_cRef;                 // reference count
    GUID                            m_guiType;
    GUID                            m_guiSubtype;
    DWORD                           m_dwIdentifier;
    ComPtr<PpboxMediaSink>          m_pSink;             // Parent media Sink
    ComPtr<IMFMediaType>            m_pMediaType;
    ComPtr<IMFMediaEventQueue>      m_pEventQueue;         // Event generator helper

    State   m_state;
    BOOL    m_IsShutdown;   // Flag to indicate if Shutdown() method was called.
    BOOL    m_bActive;      // Is the stream active?
    BOOL    m_bEOS;         // Did the Sink reach the end of the stream?
    MFTIME  m_StartTime;    // Presentation time when the clock started.
    BOOL    m_fGetStartTimeFromSample;
};


