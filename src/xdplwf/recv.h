//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include "ec.h"

typedef struct _XDP_LWF_GENERIC_RX_QUEUE {
    XDP_RX_QUEUE_HANDLE XdpRxQueue;
    XDP_RING *FrameRing;
    XDP_RING *FragmentRing;
    XDP_EXTENSION BufferVaExtension;
    XDP_EXTENSION FragmentExtension;

    //
    // For RX inspect, the EcLock provides mutual exclusion on the data path,
    // but does not provide synchronization for the above primitive fields; RSS
    // and XDP queues can be created, deleted, attached, and detached without
    // contending the lock.
    //

    UCHAR *FragmentBuffer;
    UINT32 FragmentBufferSize;
    UINT8 FragmentLimit;
    UINT8 FragmentBufferInUse;

    KSPIN_LOCK EcLock;

    //
    // For TX inspect, the data path is not expected to be affinitized to the
    // RSS processor. Instead of using the EcLock to provide XDP mutual
    // exclusion and processing inline, use an adaptive model instead, where
    // either the inline thread or the worker thread (or neither) owns the XDP
    // execution context.
    //
    // When the first NBL chain arrives on the data path, process it inline
    // without holding the EcLock. If more NBL chains arrive while the first
    // chain is being processed, append those to the TxInspectNblQueue. Once the
    // inline inspection has completed, check if more NBLs are pending on the
    // queue: if there are, start the TX inspection worker. The worker pulls
    // NBLs off the TxInspectNblQueue and processes them in small batches.
    //
    // The XDP flush request notification also needs careful handling: the
    // EcLock is not sufficient for mutual exclusion, so indicate the flush
    // request to the current owner of the TX inspection mutex.
    //
    XDP_EC TxInspectEc;
    NBL_QUEUE TxInspectNblQueue;
    NBL_QUEUE TxInspectPollNblQueue;

    XDP_LWF_GENERIC *Generic;
    struct {
        BOOLEAN TxInspect : 1;
        BOOLEAN TxInspectInline: 1;
        BOOLEAN TxInspectWorker : 1;
        BOOLEAN TxInspectNeedFlush : 1;
    } Flags;
    UINT32 QueueId;
    XDP_LIFETIME_ENTRY DeleteEntry;
    KEVENT *DeleteComplete;
} XDP_LWF_GENERIC_RX_QUEUE;

FILTER_RETURN_NET_BUFFER_LISTS XdpGenericReturnNetBufferLists;
FILTER_RECEIVE_NET_BUFFER_LISTS XdpGenericReceiveNetBufferLists;

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpGenericRxCreateQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_RX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceRxQueue,
    _Out_ CONST XDP_INTERFACE_RX_QUEUE_DISPATCH **InterfaceRxQueueDispatch
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpGenericRxActivateQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue,
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue,
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE Config
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpGenericRxDeleteQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceRxQueue
    );

#define XDP_LWF_GENERIC_INSPECT_FLAG_DISPATCH   0x1
#define XDP_LWF_GENERIC_INSPECT_FLAG_RESOURCES  0x2
#define XDP_LWF_GENERIC_INSPECT_FLAG_TX         0x4
#define XDP_LWF_GENERIC_INSPECT_FLAG_TX_WORKER  0x8

C_ASSERT(XDP_LWF_GENERIC_INSPECT_FLAG_DISPATCH == NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL);
C_ASSERT(XDP_LWF_GENERIC_INSPECT_FLAG_DISPATCH == NDIS_SEND_FLAGS_DISPATCH_LEVEL);
C_ASSERT(XDP_LWF_GENERIC_INSPECT_FLAG_RESOURCES == NDIS_RECEIVE_FLAGS_RESOURCES);

#define XDP_LWF_GENERIC_INSPECT_NDIS_RX_MASK \
    (XDP_LWF_GENERIC_INSPECT_FLAG_DISPATCH | XDP_LWF_GENERIC_INSPECT_FLAG_RESOURCES)

#define XDP_LWF_GENERIC_INSPECT_NDIS_TX_MASK \
    (XDP_LWF_GENERIC_INSPECT_FLAG_DISPATCH)

VOID
XdpGenericReceive(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NET_BUFFER_LIST *NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _Out_ NBL_COUNTED_QUEUE *PassList,
    _Out_ NBL_QUEUE *DropList,
    _In_ UINT32 XdpInspectFlags
    );