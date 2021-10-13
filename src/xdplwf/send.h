//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once

#include "ec.h"

typedef struct _XDP_LWF_GENERIC XDP_LWF_GENERIC;

typedef struct _XDP_LWF_GENERIC_TX_QUEUE {
    ULONG QueueId;
    LIST_ENTRY Link;
    XDP_LWF_GENERIC *Generic;
    XDP_TX_QUEUE_NOTIFY_HANDLE XdpNotifyHandle;

    NDIS_HANDLE FilterHandle;
    XDP_TX_QUEUE_HANDLE XdpTxQueue;
    XDP_RING *FrameRing;
    XDP_RING *CompletionRing;
    XDP_EXTENSION BufferMdlExtension;

    XDP_LWF_GENERIC_RSS_QUEUE *RssQueue;
    XDP_EC Ec;

    struct {
        BOOLEAN Pause : 1;
        BOOLEAN RxInject : 1;
    } Flags;

    KEVENT *PauseComplete;

    ULONG BufferCount;
    ULONG OutstandingCount;
    SLIST_HEADER NblComplete;
    NET_BUFFER_LIST *FreeNbls;
    NDIS_HANDLE NblPool;
    XDP_LIFETIME_ENTRY DeleteEntry;
    KEVENT *DeleteComplete;
} XDP_LWF_GENERIC_TX_QUEUE;

FILTER_SEND_NET_BUFFER_LISTS XdpGenericSendNetBufferLists;
FILTER_SEND_NET_BUFFER_LISTS_COMPLETE XdpGenericSendNetBufferListsComplete;

_IRQL_requires_(DISPATCH_LEVEL)
NET_BUFFER_LIST *
XdpGenericInjectNetBufferListsComplete(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ NET_BUFFER_LIST *NetBufferLists
    );

_IRQL_requires_(DISPATCH_LEVEL)
VOID
XdpGenericTxFlushRss(
    _In_ XDP_LWF_GENERIC_RSS_QUEUE *Queue,
    _In_ ULONG CurrentProcessor
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&Generic->Lock)
VOID
XdpGenericTxPause(
    _In_ XDP_LWF_GENERIC *Generic
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
_Requires_exclusive_lock_held_(&Generic->Lock)
VOID
XdpGenericTxRestart(
    _In_ XDP_LWF_GENERIC *Generic,
    _In_ UINT32 NewMtu
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
NTSTATUS
XdpGenericTxCreateQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceContext,
    _Inout_ XDP_TX_QUEUE_CONFIG_CREATE Config,
    _Out_ XDP_INTERFACE_HANDLE *InterfaceTxQueue,
    _Out_ CONST XDP_INTERFACE_TX_QUEUE_DISPATCH **InterfaceTxQueueDispatch
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpGenericTxActivateQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue,
    _In_ XDP_TX_QUEUE_HANDLE XdpTxQueue,
    _In_ XDP_TX_QUEUE_CONFIG_ACTIVATE Config
    );

_IRQL_requires_max_(PASSIVE_LEVEL)
VOID
XdpGenericTxDeleteQueue(
    _In_ XDP_INTERFACE_HANDLE InterfaceTxQueue
    );