/*++

    Copyright (c) Microsoft Corporation.
    Licensed under the MIT License.

Abstract:

    QUIC Perf Server declaration. Defines the functions and
    variables used in the PerfServer class.

--*/

#pragma once

#include "SecNetPerf.h"
#include "Tcp.h"

class PerfServer {
public:
    PerfServer(const QUIC_CREDENTIAL_CONFIG* CredConfig) :
        Engine(TcpAcceptCallback, TcpConnectCallback, TcpReceiveCallback, TcpSendCompleteCallback),
        Server(&Engine, CredConfig, this) {
        CxPlatZeroMemory(&LocalAddr, sizeof(LocalAddr));
        QuicAddrSetFamily(&LocalAddr, QUIC_ADDRESS_FAMILY_UNSPEC);
        QuicAddrSetPort(&LocalAddr, PERF_DEFAULT_PORT);
        InitStatus =
            Configuration.IsValid() ?
                Configuration.LoadCredential(CredConfig) :
                Configuration.GetInitStatus();
    }

    ~PerfServer() {
        if (TeardownBinding) {
            CxPlatSocketDelete(TeardownBinding);
            TeardownBinding = nullptr;
        }
        if (DataBuffer) {
            CXPLAT_FREE(DataBuffer, QUIC_POOL_PERF);
        }
    }

    QUIC_STATUS
    Init(
        _In_ int argc,
        _In_reads_(argc) _Null_terminated_ char* argv[],
        _In_ CXPLAT_DATAPATH* Datapath
        );
    QUIC_STATUS Start(_In_ CXPLAT_EVENT* StopEvent);
    void Wait(int Timeout);

    static CXPLAT_DATAPATH_RECEIVE_CALLBACK DatapathReceive;
    static CXPLAT_DATAPATH_UNREACHABLE_CALLBACK DatapathUnreachable;

private:

    struct TcpConnectionContext {
        PerfServer* Server;
        CxPlatHashTable StreamTable;
        TcpConnectionContext(PerfServer* Server) : Server(Server) { }
    };

    CxPlatPoolT<TcpConnectionContext> TcpConnectionContextAllocator;

    struct StreamContext {
        StreamContext(
            PerfServer* Server, bool Unidirectional, bool BufferedIo) :
            Server{Server}, Unidirectional{Unidirectional}, BufferedIo{BufferedIo} {
            if (BufferedIo) {
                IdealSendBuffer = 1; // Hack to get just do 1 send at a time.
            }
        }
        CXPLAT_HASHTABLE_ENTRY Entry; // To TCP StreamTable
        PerfServer* Server;
        const bool Unidirectional;
        const bool BufferedIo;
        bool ResponseSizeSet{false};
        bool SendShutdown{false};
        bool RecvShutdown{false};
        uint64_t IdealSendBuffer{PERF_DEFAULT_SEND_BUFFER_SIZE};
        uint64_t ResponseSize{0};
        uint64_t BytesSent{0};
        uint64_t OutstandingBytes{0};
        uint32_t IoSize{PERF_DEFAULT_IO_SIZE};
        QUIC_BUFFER LastBuffer;
    };

    CxPlatPoolT<StreamContext> StreamContextAllocator;
    CxPlatPoolT<TcpSendData> TcpSendDataAllocator;

    QUIC_STATUS
    ListenerCallback(
        _In_ MsQuicListener* Listener,
        _Inout_ QUIC_LISTENER_EVENT* Event
        );

    static
    QUIC_STATUS
    ListenerCallbackStatic(
        _In_ MsQuicListener* Listener,
        _In_ void* Context,
        _Inout_ QUIC_LISTENER_EVENT* Event
        ) {
        return ((PerfServer*)Context)->ListenerCallback(Listener, Event);
    }

    QUIC_STATUS
    ConnectionCallback(
        _In_ HQUIC ConnectionHandle,
        _Inout_ QUIC_CONNECTION_EVENT* Event
        );

    QUIC_STATUS
    StreamCallback(
        _In_ StreamContext* Context,
        _In_ HQUIC StreamHandle,
        _Inout_ QUIC_STREAM_EVENT* Event
        );

    void
    SendResponse(
        _In_ StreamContext* Context,
        _In_ HQUIC StreamHandle
        );

    QUIC_STATUS InitStatus;
    MsQuicRegistration Registration {
        "secnetperf-server",
        PerfDefaultExecutionProfile,
        true};
    MsQuicAlpn Alpn {PERF_ALPN};
    MsQuicConfiguration Configuration {
        Registration,
        Alpn,
        MsQuicSettings()
            .SetConnFlowControlWindow(PERF_DEFAULT_CONN_FLOW_CONTROL)
            .SetPeerBidiStreamCount(PERF_DEFAULT_STREAM_COUNT)
            .SetPeerUnidiStreamCount(PERF_DEFAULT_STREAM_COUNT)
            .SetDisconnectTimeoutMs(PERF_DEFAULT_DISCONNECT_TIMEOUT)
            .SetIdleTimeoutMs(PERF_DEFAULT_IDLE_TIMEOUT)
            .SetSendBufferingEnabled(false)
            .SetServerResumptionLevel(QUIC_SERVER_RESUME_AND_ZERORTT)
            .SetCongestionControlAlgorithm(PerfDefaultCongestionControl)
            .SetEcnEnabled(PerfDefaultEcnEnabled)
            .SetEncryptionOffloadAllowed(PerfDefaultQeoAllowed)
            .SetOneWayDelayEnabled(true)};
    MsQuicListener Listener {Registration, CleanUpManual, ListenerCallbackStatic, this};
    QUIC_ADDR LocalAddr;
    CXPLAT_EVENT* StopEvent {nullptr};
    QUIC_BUFFER* DataBuffer {nullptr};
    uint8_t PrintStats {FALSE};

    TcpEngine Engine;
    TcpServer Server;

    uint32_t CibirIdLength {0};
    uint8_t CibirId[7]; // {offset, values}

    CXPLAT_SOCKET* TeardownBinding {nullptr};

    void
    SendTcpResponse(
        _In_ StreamContext* Context,
        _In_ TcpConnection* Connection
        );

    _IRQL_requires_max_(DISPATCH_LEVEL)
    _Function_class_(TcpAcceptCallback)
    static
    void
    TcpAcceptCallback(
        _In_ TcpServer* Server,
        _In_ TcpConnection* Connection
        );

    _IRQL_requires_max_(DISPATCH_LEVEL)
    _Function_class_(TcpConnectCallback)
    static
    void
    TcpConnectCallback(
        _In_ TcpConnection* Connection,
        bool IsConnected
        );

    _IRQL_requires_max_(DISPATCH_LEVEL)
    _Function_class_(TcpReceiveCallback)
    static
    void
    TcpReceiveCallback(
        _In_ TcpConnection* Connection,
        uint32_t StreamID,
        bool Open,
        bool Fin,
        bool Abort,
        uint32_t Length,
        uint8_t* Buffer
        );

    _IRQL_requires_max_(DISPATCH_LEVEL)
    _Function_class_(TcpSendCompleteCallback)
    static
    void
    TcpSendCompleteCallback(
        _In_ TcpConnection* Connection,
        TcpSendData* SendDataChain
        );
};
