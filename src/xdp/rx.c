//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#include "precomp.h"
#include "rx.tmh"

static XDP_REG_WATCHER_CLIENT_ENTRY XdpRxRegWatcherEntry;
static UINT32 XdpRxRingSize = 32;

typedef enum _XDP_RX_QUEUE_STATE {
    XdpRxQueueStateUnbound,
    XdpRxQueueStateActive,
} XDP_RX_QUEUE_STATE;

typedef struct _XDP_RX_QUEUE_KEY {
    XDP_HOOK_ID HookId;
    UINT32 QueueId;
} XDP_RX_QUEUE_KEY;

typedef struct _XDP_RX_QUEUE {
    XDP_PROGRAM *Program;

    XDP_RX_QUEUE_DISPATCH Dispatch;
    XDP_RING *FrameRing;
    XDP_RING *FragmentRing;
    XDP_EXTENSION VirtualAddressExtension;
    XDP_EXTENSION FragmentExtension;
    XDP_EXTENSION RxActionExtension;

#if DBG
    //
    // Tracks the internally-consumed frames. We use this to verify a single
    // XDP packet has been enqueued to the head of the XDP receive queue when
    // using the single frame receive API.
    //
    // FrameRing->ConsumerIndex == FrameRing->ProducerIndex - 1.
    //
    UINT32 FrameConsumerIndex;

    //
    // XDP interface execution context diagnostics block.
    //
    XDP_DBG_QUEUE_EC DbgEc;
#endif

    //
    // Metadata to group batches of redirected frames together.
    //
    XDP_REDIRECT_CONTEXT RedirectContext;

    //
    // The pending data path / control path serialization callback.
    //
    XDP_QUEUE_SYNC Sync;

    //
    // Control path fields. TODO: Move to a separate, paged structure.
    //

    ULONG ReferenceCount;
    XDP_BINDING_HANDLE Binding;
    XDP_RX_QUEUE_KEY Key;
    XDP_BINDING_CLIENT_ENTRY BindingClientEntry;
    XDP_RX_QUEUE_STATE State;
    XDP_RX_CAPABILITIES InterfaceRxCapabilities;
    XDP_INTERFACE_HANDLE InterfaceRxQueue;
    CONST XDP_INTERFACE_RX_QUEUE_DISPATCH *InterfaceRxDispatch;
    NDIS_HANDLE InterfaceRxPollHandle;

    XDP_EXTENSION_SET *FrameExtensionSet;
    XDP_EXTENSION_SET *BufferExtensionSet;

    XDP_QUEUE_INFO QueueInfo;
    XDP_RX_QUEUE_CONFIG_CREATE_DETAILS ConfigCreate;
    XDP_RX_QUEUE_CONFIG_ACTIVATE_DETAILS ConfigActivate;

    LIST_ENTRY NotifyClients;
} XDP_RX_QUEUE;

static
XDP_RX_QUEUE *
XdpRxQueueFromHandle(
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    )
{
    return CONTAINING_RECORD(XdpRxQueue, XDP_RX_QUEUE, Dispatch);
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdppFlushReceive(
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    XDP_RING *FrameRing = RxQueue->FrameRing;

    XdpFlushRedirect(&RxQueue->RedirectContext);

    //
    // We've removed all references to the internally buffered frames, so
    // release the elements back to the interface.
    //
#if DBG
    ASSERT(RxQueue->FrameConsumerIndex == FrameRing->ProducerIndex);
#endif
    FrameRing->ConsumerIndex = FrameRing->ProducerIndex;

    if (RxQueue->FragmentRing != NULL) {
        RxQueue->FragmentRing->ConsumerIndex = RxQueue->FragmentRing->ProducerIndex;
    }

    XdpQueueDatapathSync(&RxQueue->Sync);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpFlushReceive(
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromHandle(XdpRxQueue);

    XdbgEnterQueueEc(RxQueue, TRUE);
    XdppFlushReceive(RxQueue);
    XdbgExitQueueEc(RxQueue);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
XDP_RX_ACTION
XdpReceive(
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromHandle(XdpRxQueue);
    XDP_RING *FrameRing = RxQueue->FrameRing;
    XDP_RING *FragmentRing = RxQueue->FragmentRing;
    UINT32 FrameIndex = (FrameRing->ProducerIndex - 1) & FrameRing->Mask;
    UINT32 FragmentIndex = 0;
    XDP_FRAME *Frame;
    XDP_FRAME_FRAGMENT *Fragment = NULL;
    XDP_RX_ACTION Action;

    XdbgEnterQueueEc(RxQueue, FALSE);

    //
    // XdpReceive assumes a single element queued at a time. Look at the element
    // most recently pushed onto the ring and flush on behalf of the caller only
    // when the queue becomes full.
    //
#if DBG
    ASSERT(RxQueue->FrameConsumerIndex + 1 == FrameRing->ProducerIndex);
#endif

    Frame = XdpRingGetElement(FrameRing, FrameIndex);

    if (FragmentRing != NULL) {
        Fragment = XdpGetFragmentExtension(Frame, &RxQueue->FragmentExtension);
        FragmentIndex = FragmentRing->ProducerIndex - Fragment->FragmentBufferCount;
        FragmentIndex &= FragmentRing->Mask;
    }

    Action =
        XdpInspect(
            RxQueue->Program, &RxQueue->RedirectContext, RxQueue->FrameRing, FrameIndex,
            RxQueue->FragmentRing, &RxQueue->FragmentExtension, FragmentIndex,
            &RxQueue->VirtualAddressExtension);

    if (Action != XDP_RX_ACTION_PEND) {
        //
        // We're not pending the packet, so free the head of the frame
        // ring back to the XDP interface for reuse.
        //
        FrameRing->ProducerIndex--;

        if (FragmentRing != NULL) {
            FragmentRing->ProducerIndex -= Fragment->FragmentBufferCount;
        }
    } else {
#if DBG
        RxQueue->FrameConsumerIndex++;
#endif
    }

    //
    // Finally, if we've filled up the receive queue, perform a flush on
    // behalf of the caller.
    //
    if (XdpRingFree(FrameRing) == 0 || (FragmentRing != NULL && XdpRingFree(FragmentRing) == 0)) {
        XdppFlushReceive(RxQueue);
    }

    XdbgExitQueueEc(RxQueue);

    return Action;
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdppReceiveBatch(
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    XDP_RING *FrameRing = RxQueue->FrameRing;

    ASSERT(RxQueue->InterfaceRxCapabilities.RxBatchingEnabled);

    //
    // XdpReceiveBatch makes no assumptions on the number of elements queued at
    // a time. Look at all elements in the ring and always flush on behalf of
    // the caller.
    //

    while (XdpRingCount(FrameRing) > 0) {
        UINT32 FrameIndex = FrameRing->ConsumerIndex & FrameRing->Mask;
        UINT32 FragmentIndex = 0;
        XDP_FRAME *Frame;
        XDP_RX_ACTION Action;
        XDP_FRAME_FRAGMENT *Fragment = NULL;
        XDP_FRAME_RX_ACTION *ActionExtension;

        Frame = XdpRingGetElement(FrameRing, FrameIndex);

        if (RxQueue->FragmentRing != NULL) {
            FragmentIndex = RxQueue->FragmentRing->ConsumerIndex & RxQueue->FragmentRing->Mask;
            Fragment = XdpGetFragmentExtension(Frame, &RxQueue->FragmentExtension);
        }

        Action =
            XdpInspect(
                RxQueue->Program, &RxQueue->RedirectContext, RxQueue->FrameRing, FrameIndex,
                RxQueue->FragmentRing, &RxQueue->FragmentExtension, FragmentIndex,
                &RxQueue->VirtualAddressExtension);

        ActionExtension = XdpGetRxActionExtension(Frame, &RxQueue->RxActionExtension);
        ActionExtension->RxAction = Action;

        FrameRing->ConsumerIndex++;

        if (RxQueue->FragmentRing != NULL) {
            RxQueue->FragmentRing->ConsumerIndex += Fragment->FragmentBufferCount;
        }

#if DBG
        RxQueue->FrameConsumerIndex = FrameRing->ConsumerIndex;
#endif
    }

    XdppFlushReceive(RxQueue);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpReceiveBatch(
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromHandle(XdpRxQueue);

    XdbgEnterQueueEc(RxQueue, TRUE);
    XdppReceiveBatch(RxQueue);
    XdbgExitQueueEc(RxQueue);
}

static
_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpRxQueueExclusiveFlush(
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    //
    // Perform a flush after an exclusive RX batch handler has completed. The
    // batch handler is responsible for processing all frames on the ring, but
    // we still need handle the sync.
    //

    ASSERT(RxQueue->FrameRing->ConsumerIndex == RxQueue->FrameRing->ProducerIndex);

    if (RxQueue->FragmentRing != NULL) {
        ASSERT(RxQueue->FragmentRing->ConsumerIndex == RxQueue->FragmentRing->ProducerIndex);
    }

#if DBG
    RxQueue->FrameConsumerIndex = RxQueue->FrameRing->ConsumerIndex;
#endif

    XdpQueueDatapathSync(&RxQueue->Sync);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
XdpReceiveXskExclusiveBatch(
    _In_ XDP_RX_QUEUE_HANDLE XdpRxQueue
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromHandle(XdpRxQueue);

    XdbgEnterQueueEc(RxQueue, TRUE);
    ASSERT(RxQueue->InterfaceRxCapabilities.RxBatchingEnabled);

    //
    // Attempt to pass the entire batch to XSK.
    //
    if (XskReceiveBatchedExclusive(XdpProgramGetXskBypassTarget(RxQueue->Program))) {
        XdpRxQueueExclusiveFlush(RxQueue);
    } else {
        //
        // XSK could not process the batch, so fall back to the common code path.
        //
        XdppReceiveBatch(RxQueue);
    }

    XdbgExitQueueEc(RxQueue);
}

static CONST XDP_RX_QUEUE_DISPATCH XdpRxDispatch = {
    .Receive        = XdpReceive,
    .ReceiveBatch   = XdpReceiveBatch,
    .FlushReceive   = XdpFlushReceive,
};

//
// This dispatch table optimizes the case with a single XSK receiving all
// traffic.
//
static CONST XDP_RX_QUEUE_DISPATCH XdpRxExclusiveXskDispatch = {
    .Receive        = XdpReceive,
    .ReceiveBatch   = XdpReceiveXskExclusiveBatch,
    .FlushReceive   = XdpFlushReceive,
};

//
// The RX queue control path.
//

static
VOID
XdpRxQueueReference(
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    NT_VERIFY(RxQueue->ReferenceCount++ > 0);
}

static CONST XDP_EXTENSION_REGISTRATION XdpRxFrameExtensions[] = {
    {
        .Info.ExtensionName     = XDP_FRAME_EXTENSION_FRAGMENT_NAME,
        .Info.ExtensionVersion  = XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_FRAME,
        .Size                   = sizeof(XDP_FRAME_FRAGMENT),
        .Alignment              = __alignof(XDP_FRAME_FRAGMENT),
    },
    {
        .Info.ExtensionName     = XDP_FRAME_EXTENSION_RX_ACTION_NAME,
        .Info.ExtensionVersion  = XDP_FRAME_EXTENSION_RX_ACTION_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_FRAME,
        .Size                   = sizeof(XDP_FRAME_RX_ACTION),
        .Alignment              = __alignof(XDP_FRAME_RX_ACTION),
    },
    {
        .Info.ExtensionName     = XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_NAME,
        .Info.ExtensionVersion  = XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_FRAME,
        .Size                   = 0,
        .Alignment              = __alignof(UCHAR),
    },
};

static CONST XDP_EXTENSION_REGISTRATION XdpRxBufferExtensions[] = {
    {
        .Info.ExtensionName     = XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME,
        .Info.ExtensionVersion  = XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_BUFFER,
        .Size                   = sizeof(XDP_BUFFER_VIRTUAL_ADDRESS),
        .Alignment              = __alignof(XDP_BUFFER_VIRTUAL_ADDRESS),
    },
    {
        .Info.ExtensionName     = XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_NAME,
        .Info.ExtensionVersion  = XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_VERSION_1,
        .Info.ExtensionType     = XDP_EXTENSION_TYPE_BUFFER,
        .Size                   = 0,
        .Alignment              = __alignof(UCHAR),
    },
};

static
XDP_RX_QUEUE *
XdpRxQueueFromConfigCreate(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    )
{
    return CONTAINING_RECORD(RxQueueConfig, XDP_RX_QUEUE, ConfigCreate);
}

static
XDP_EXTENSION_SET *
XdpRxQueueGetExtensionSet(
    _In_ XDP_RX_QUEUE *RxQueue,
    _In_ XDP_EXTENSION_TYPE ExtensionType
    )
{
    XDP_EXTENSION_SET *ExtensionSet = NULL;

    switch (ExtensionType) {

    case XDP_EXTENSION_TYPE_FRAME:
        ExtensionSet = RxQueue->FrameExtensionSet;
        break;

    case XDP_EXTENSION_TYPE_BUFFER:
        ExtensionSet = RxQueue->BufferExtensionSet;
        break;

    default:
        FRE_ASSERT(FALSE);

    }

    return ExtensionSet;
}

CONST XDP_QUEUE_INFO *
XdpRxQueueGetTargetQueueInfo(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromConfigCreate(RxQueueConfig);

    return &RxQueue->QueueInfo;
}

VOID
XdpRxQueueSetCapabilities(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_RX_CAPABILITIES *Capabilities
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromConfigCreate(RxQueueConfig);

    //
    // TODO: Some capabilities are not implemented.
    //
    FRE_ASSERT(Capabilities->Header.Revision >= XDP_RX_CAPABILITIES_REVISION_1);
    FRE_ASSERT(Capabilities->Header.Size >= XDP_SIZEOF_RX_CAPABILITIES_REVISION_1);

    RxQueue->InterfaceRxCapabilities = *Capabilities;

    //
    // XDP programs require a system virtual address. Ensure the driver has
    // registered the capability and enable the extension.
    //
    FRE_ASSERT(Capabilities->VirtualAddressSupported);
    XdpExtensionSetEnableEntry(RxQueue->BufferExtensionSet, XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME);

    if (Capabilities->MaximumFragments > 0) {
        XdpExtensionSetEnableEntry(RxQueue->FrameExtensionSet, XDP_FRAME_EXTENSION_FRAGMENT_NAME);
    }

    if (Capabilities->RxBatchingEnabled) {
        XdpExtensionSetEnableEntry(RxQueue->FrameExtensionSet, XDP_FRAME_EXTENSION_RX_ACTION_NAME);
    }
}

VOID
XdpRxQueueRegisterExtensionVersion(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo
    )
{
    FRE_ASSERT(ExtensionInfo->Header.Revision >= XDP_EXTENSION_INFO_REVISION_1);
    FRE_ASSERT(ExtensionInfo->Header.Size >= XDP_SIZEOF_EXTENSION_INFO_REVISION_1);

    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromConfigCreate(RxQueueConfig);
    XDP_EXTENSION_SET *Set = XdpRxQueueGetExtensionSet(RxQueue, ExtensionInfo->ExtensionType);

    TraceInfo(
        TRACE_CORE,
        "RxQueue=%p ExtensionName=%S ExtensionVersion=%u ExtensionType=%!EXTENSION_TYPE!",
        RxQueue, ExtensionInfo->ExtensionName, ExtensionInfo->ExtensionVersion,
        ExtensionInfo->ExtensionType);
    XdpExtensionSetRegisterEntry(Set, ExtensionInfo);
}

VOID
XdpRxQueueSetDescriptorContexts(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_RX_DESCRIPTOR_CONTEXTS *Descriptors
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromConfigCreate(RxQueueConfig);

    FRE_ASSERT(Descriptors->Header.Revision >= XDP_RX_DESCRIPTOR_CONTEXTS_REVISION_1);
    FRE_ASSERT(Descriptors->Header.Size >= XDP_SIZEOF_RX_DESCRIPTOR_CONTEXTS_REVISION_1);

    FRE_ASSERT((Descriptors->FrameContextSize == 0) == (Descriptors->FrameContextAlignment == 0));
    FRE_ASSERT((Descriptors->BufferContextSize == 0) == (Descriptors->BufferContextAlignment == 0));

    if (Descriptors->FrameContextSize > 0) {
        XdpExtensionSetResizeEntry(
            RxQueue->FrameExtensionSet, XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_NAME,
            Descriptors->FrameContextSize, Descriptors->FrameContextAlignment);
        XdpExtensionSetEnableEntry(
            RxQueue->FrameExtensionSet, XDP_FRAME_EXTENSION_INTERFACE_CONTEXT_NAME);
    }

    if (Descriptors->BufferContextSize > 0) {
        XdpExtensionSetResizeEntry(
            RxQueue->BufferExtensionSet, XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_NAME,
            Descriptors->BufferContextSize, Descriptors->BufferContextAlignment);
        XdpExtensionSetEnableEntry(
            RxQueue->BufferExtensionSet, XDP_BUFFER_EXTENSION_INTERFACE_CONTEXT_NAME);
    }
}

VOID
XdpRxQueueSetPollInfo(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig,
    _In_ XDP_POLL_INFO *PollInfo
    )
{
    FRE_ASSERT(PollInfo->Header.Revision >= XDP_POLL_INFO_REVISION_1);
    FRE_ASSERT(PollInfo->Header.Size >= XDP_SIZEOF_POLL_INFO_REVISION_1);

    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromConfigCreate(RxQueueConfig);

    RxQueue->InterfaceRxPollHandle = PollInfo->PollHandle;
}

XDP_RX_QUEUE *
XdpRxQueueFromConfigActivate(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    )
{
    return CONTAINING_RECORD(RxQueueConfig, XDP_RX_QUEUE, ConfigActivate);
}

XDP_RING *
XdpRxQueueGetFrameRing(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromConfigActivate(RxQueueConfig);

    return RxQueue->FrameRing;
}

XDP_RING *
XdpRxQueueGetFragmentRing(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromConfigActivate(RxQueueConfig);

    FRE_ASSERT(RxQueue->FragmentRing != NULL);

    return RxQueue->FragmentRing;
}

VOID
XdpRxQueueGetExtension(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig,
    _In_ XDP_EXTENSION_INFO *ExtensionInfo,
    _Out_ XDP_EXTENSION *Extension
    )
{
    FRE_ASSERT(ExtensionInfo->Header.Revision >= XDP_EXTENSION_INFO_REVISION_1);
    FRE_ASSERT(ExtensionInfo->Header.Size >= XDP_SIZEOF_EXTENSION_INFO_REVISION_1);

    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromConfigActivate(RxQueueConfig);
    XDP_EXTENSION_SET *Set = XdpRxQueueGetExtensionSet(RxQueue, ExtensionInfo->ExtensionType);

    XdpExtensionSetGetExtension(Set, ExtensionInfo, Extension);
}

BOOLEAN
XdpRxQueueIsVirtualAddressEnabled(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromConfigActivate(RxQueueConfig);

    return
        XdpExtensionSetIsExtensionEnabled(
            RxQueue->BufferExtensionSet, XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME);
}

BOOLEAN
XdpRxQueueIsRxBatchEnabled(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromConfigActivate(RxQueueConfig);

    return
        RxQueue->InterfaceRxCapabilities.RxBatchingEnabled &&
        XdpExtensionSetIsExtensionEnabled(
            RxQueue->FrameExtensionSet, XDP_FRAME_EXTENSION_RX_ACTION_NAME);
}

UINT8
XdpRxQueueGetMaxmimumFragments(
    _In_ XDP_RX_QUEUE_CONFIG_ACTIVATE RxQueueConfig
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromConfigActivate(RxQueueConfig);

    return RxQueue->InterfaceRxCapabilities.MaximumFragments;
}

static
CONST XDP_HOOK_ID *
XdppRxQueueGetHookId(
    _In_ XDP_RX_QUEUE_CONFIG_CREATE RxQueueConfig
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromConfigCreate(RxQueueConfig);

    return &RxQueue->Key.HookId;
}

static CONST XDP_RX_QUEUE_CONFIG_RESERVED XdpRxConfigReservedDispatch = {
    .Header                         = {
        .Revision                   = XDP_RX_QUEUE_CONFIG_RESERVED_REVISION_1,
        .Size                       = XDP_SIZEOF_RX_QUEUE_CONFIG_RESERVED_REVISION_1,
    },
    .GetHookId                      = XdppRxQueueGetHookId,
};

static CONST XDP_RX_QUEUE_CONFIG_CREATE_DISPATCH XdpRxConfigCreateDispatch = {
    .Header                     = {
        .Revision               = XDP_RX_QUEUE_CONFIG_CREATE_DISPATCH_REVISION_1,
        .Size                   = XDP_SIZEOF_RX_QUEUE_CONFIG_CREATE_DISPATCH_REVISION_1
    },
    .Reserved                   = &XdpRxConfigReservedDispatch,
    .GetTargetQueueInfo         = XdpRxQueueGetTargetQueueInfo,
    .SetRxQueueCapabilities     = XdpRxQueueSetCapabilities,
    .RegisterExtensionVersion   = XdpRxQueueRegisterExtensionVersion,
    .SetRxDescriptorContexts    = XdpRxQueueSetDescriptorContexts,
    .SetPollInfo                = XdpRxQueueSetPollInfo,
};

static CONST XDP_RX_QUEUE_CONFIG_ACTIVATE_DISPATCH XdpRxConfigActivateDispatch = {
    .Header                     = {
        .Revision               = XDP_RX_QUEUE_CONFIG_ACTIVATE_DISPATCH_REVISION_1,
        .Size                   = XDP_SIZEOF_RX_QUEUE_CONFIG_ACTIVATE_DISPATCH_REVISION_1
    },
    .GetFrameRing               = XdpRxQueueGetFrameRing,
    .GetFragmentRing            = XdpRxQueueGetFragmentRing,
    .GetExtension               = XdpRxQueueGetExtension,
    .IsVirtualAddressEnabled    = XdpRxQueueIsVirtualAddressEnabled,
};

static
VOID
XdpRxQueueNotifyClients(
    _In_ XDP_RX_QUEUE *RxQueue,
    _In_ XDP_RX_QUEUE_NOTIFICATION_TYPE NotificationType
    )
{
    LIST_ENTRY *Entry = RxQueue->NotifyClients.Flink;

    TraceInfo(
        TRACE_CORE, "RxQueue=%p NotificationType=%!RX_QUEUE_NOTIFICATION_TYPE!",
        RxQueue, NotificationType);

    while (Entry != &RxQueue->NotifyClients) {
        XDP_RX_QUEUE_NOTIFICATION_ENTRY *NotifyEntry;

        NotifyEntry = CONTAINING_RECORD(Entry, XDP_RX_QUEUE_NOTIFICATION_ENTRY, Link);
        Entry = Entry->Flink;

        NotifyEntry->NotifyRoutine(NotifyEntry, NotificationType);
    }
}

static
VOID
XdpRxQueueDetachInterface(
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    if (RxQueue->InterfaceRxQueue != NULL) {
        XdpRxQueueNotifyClients(RxQueue, XDP_RX_QUEUE_NOTIFICATION_DETACH);
        XdpIfDeleteRxQueue(RxQueue->Binding, RxQueue->InterfaceRxQueue);
        RxQueue->State = XdpRxQueueStateUnbound;
        XdpRxQueueNotifyClients(RxQueue, XDP_RX_QUEUE_NOTIFICATION_DETACH_COMPLETE);

        RxQueue->InterfaceRxDispatch = NULL;
        RxQueue->InterfaceRxQueue = NULL;
    } else {
        ASSERT(RxQueue->State == XdpRxQueueStateUnbound);
        ASSERT(RxQueue->InterfaceRxDispatch == NULL);
        ASSERT(RxQueue->InterfaceRxQueue == NULL);
    }

#if DBG
    RxQueue->FrameConsumerIndex = 0;
#endif

    if (RxQueue->FragmentRing != NULL) {
        XdpRingFreeRing(RxQueue->FragmentRing);
        RxQueue->FragmentRing = NULL;
    }

    if (RxQueue->FrameRing != NULL) {
        XdpRingFreeRing(RxQueue->FrameRing);
        RxQueue->FrameRing = NULL;
    }

    if (RxQueue->BufferExtensionSet != NULL) {
        XdpExtensionSetCleanup(RxQueue->BufferExtensionSet);
        RxQueue->BufferExtensionSet = NULL;
    }

    if (RxQueue->FrameExtensionSet != NULL) {
        XdpExtensionSetCleanup(RxQueue->FrameExtensionSet);
        RxQueue->FrameExtensionSet = NULL;
    }
}

static
NTSTATUS
XdpRxQueueAttachInterface(
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    NTSTATUS Status;
    XDP_RX_QUEUE_CONFIG_ACTIVATE ConfigActivate =
        (XDP_RX_QUEUE_CONFIG_ACTIVATE)&RxQueue->ConfigActivate;
    XDP_EXTENSION_INFO ExtensionInfo;
    UINT32 BufferSize, FrameSize, FrameOffset;
    UINT8 BufferAlignment, FrameAlignment;

    ASSERT(RxQueue->State == XdpRxQueueStateUnbound);
    ASSERT(RxQueue->InterfaceRxQueue == NULL);

    TraceEnter(TRACE_CORE, "RxQueue=%p", RxQueue);

    Status =
        XdpExtensionSetCreate(
            XDP_EXTENSION_TYPE_FRAME, XdpRxFrameExtensions, RTL_NUMBER_OF(XdpRxFrameExtensions),
            &RxQueue->FrameExtensionSet);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpExtensionSetCreate(
            XDP_EXTENSION_TYPE_BUFFER, XdpRxBufferExtensions, RTL_NUMBER_OF(XdpRxBufferExtensions),
            &RxQueue->BufferExtensionSet);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    RxQueue->ConfigCreate.Dispatch = &XdpRxConfigCreateDispatch;

    Status =
        XdpIfCreateRxQueue(
            RxQueue->Binding, (XDP_RX_QUEUE_CONFIG_CREATE)&RxQueue->ConfigCreate,
            &RxQueue->InterfaceRxQueue, &RxQueue->InterfaceRxDispatch);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    //
    // Ensure the interface driver has registered its capabilities.
    //
    FRE_ASSERT(RxQueue->InterfaceRxCapabilities.Header.Revision >= XDP_RX_CAPABILITIES_REVISION_1);
    FRE_ASSERT(RxQueue->InterfaceRxCapabilities.Header.Size >= XDP_SIZEOF_RX_CAPABILITIES_REVISION_1);

    Status =
        XdpExtensionSetAssignLayout(
            RxQueue->BufferExtensionSet, sizeof(XDP_BUFFER), __alignof(XDP_BUFFER),
            &BufferSize, &BufferAlignment);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = RtlUInt32Add(FIELD_OFFSET(XDP_FRAME, Buffer), BufferSize, &FrameOffset);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status =
        XdpExtensionSetAssignLayout(
            RxQueue->FrameExtensionSet, FrameOffset, max(__alignof(XDP_FRAME), BufferAlignment),
            &FrameSize, &FrameAlignment);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    Status = XdpRingAllocate(FrameSize, XdpRxRingSize, FrameAlignment, &RxQueue->FrameRing);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    if (RxQueue->InterfaceRxCapabilities.MaximumFragments > 0) {
        Status =
            XdpRingAllocate(
                BufferSize, max(RxQueue->InterfaceRxCapabilities.MaximumFragments, XdpRxRingSize),
                BufferAlignment, &RxQueue->FragmentRing);
        if (!NT_SUCCESS(Status)) {
            goto Exit;
        }
    }

    RxQueue->ConfigActivate.Dispatch = &XdpRxConfigActivateDispatch;

    //
    // Implement a fast path for a single XSK receiving all frames.
    //
    if (XdpProgramCanXskBypass(RxQueue->Program)) {
        RxQueue->Dispatch = XdpRxExclusiveXskDispatch;
    } else {
        RxQueue->Dispatch = XdpRxDispatch;
    }

    XdpRxQueueNotifyClients(RxQueue, XDP_RX_QUEUE_NOTIFICATION_ATTACH);
    XdpIfActivateRxQueue(
        RxQueue->Binding, RxQueue->InterfaceRxQueue, (XDP_RX_QUEUE_HANDLE)&RxQueue->Dispatch,
        ConfigActivate);
    RxQueue->State = XdpRxQueueStateActive;

    if (XdpRxQueueIsVirtualAddressEnabled(ConfigActivate)) {
        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_NAME,
            XDP_BUFFER_EXTENSION_VIRTUAL_ADDRESS_VERSION_1, XDP_EXTENSION_TYPE_BUFFER);
        XdpRxQueueGetExtension(ConfigActivate, &ExtensionInfo, &RxQueue->VirtualAddressExtension);
    }

    if (RxQueue->FragmentRing != NULL) {
        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_FRAME_EXTENSION_FRAGMENT_NAME,
            XDP_FRAME_EXTENSION_FRAGMENT_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
        XdpRxQueueGetExtension(ConfigActivate, &ExtensionInfo, &RxQueue->FragmentExtension);
    }

    if (XdpRxQueueIsRxBatchEnabled(ConfigActivate)) {
        XdpInitializeExtensionInfo(
            &ExtensionInfo, XDP_FRAME_EXTENSION_RX_ACTION_NAME,
            XDP_FRAME_EXTENSION_RX_ACTION_VERSION_1, XDP_EXTENSION_TYPE_FRAME);
        XdpRxQueueGetExtension(ConfigActivate, &ExtensionInfo, &RxQueue->RxActionExtension);
    }

Exit:

    if (!NT_SUCCESS(Status)) {
        ASSERT(RxQueue->State == XdpRxQueueStateUnbound);
        XdpRxQueueDetachInterface(RxQueue);
    }

    TraceExitStatus(TRACE_CORE);

    return Status;
}

static
XDP_RX_QUEUE *
XdpRxQueueFromBindingEntry(
    _In_ XDP_BINDING_CLIENT_ENTRY *ClientEntry
    )
{
    return CONTAINING_RECORD(ClientEntry, XDP_RX_QUEUE, BindingClientEntry);
}

static
VOID
XdpRxQueueDetachEvent(
    _In_ XDP_BINDING_CLIENT_ENTRY *ClientEntry
    )
{
    XDP_RX_QUEUE *RxQueue = XdpRxQueueFromBindingEntry(ClientEntry);

    XdpRxQueueNotifyClients(RxQueue, XDP_RX_QUEUE_NOTIFICATION_DELETE);
}

static
CONST
XDP_BINDING_CLIENT RxQueueBindingClient = {
    .ClientId           = XDP_BINDING_CLIENT_ID_RX_QUEUE,
    .KeySize            = sizeof(XDP_RX_QUEUE_KEY),
    .BindingDetached    = XdpRxQueueDetachEvent,
};

static
VOID
XdpRxQueueInitializeKey(
    _Out_ XDP_RX_QUEUE_KEY *Key,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId
    )
{
    RtlZeroMemory(Key, sizeof(*Key));
    Key->HookId = *HookId;
    Key->QueueId = QueueId;
}

static
NTSTATUS
XdpRxQueueCreate(
    _In_ XDP_BINDING_HANDLE Binding,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _Out_ XDP_RX_QUEUE **NewRxQueue
    )
{
    XDP_RX_QUEUE_KEY Key;
    XDP_RX_QUEUE *RxQueue = NULL;
    NTSTATUS Status;

    *NewRxQueue = NULL;

    if (HookId->SubLayer != XDP_HOOK_INSPECT) {
        //
        // Chaining inspection queues onto injection queues is not implemented.
        //
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    if (!NT_VERIFY(XdpIfSupportsHookId(XdpIfGetCapabilities(Binding), HookId))) {
        Status = STATUS_NOT_SUPPORTED;
        goto Exit;
    }

    XdpRxQueueInitializeKey(&Key, HookId, QueueId);
    if (XdpIfFindClientEntry(Binding, &RxQueueBindingClient, &Key) != NULL) {
        Status = STATUS_DUPLICATE_OBJECTID;
        goto Exit;
    }

    RxQueue = ExAllocatePoolZero(NonPagedPoolNx, sizeof(*RxQueue), XDP_POOLTAG_RXQUEUE);
    if (RxQueue == NULL) {
        Status = STATUS_NO_MEMORY;
        goto Exit;
    }

    RxQueue->ReferenceCount = 1;
    RxQueue->State = XdpRxQueueStateUnbound;
    XdpIfInitializeClientEntry(&RxQueue->BindingClientEntry);
    InitializeListHead(&RxQueue->NotifyClients);
    XdpQueueSyncInitialize(&RxQueue->Sync);
    RxQueue->Binding = Binding;
    RxQueue->Key = Key;
    XdpInitializeQueueInfo(&RxQueue->QueueInfo, XDP_QUEUE_TYPE_DEFAULT_RSS, QueueId);
    XdbgInitializeQueueEc(RxQueue);

    Status =
        XdpIfRegisterClient(
            Binding, &RxQueueBindingClient, &RxQueue->Key, &RxQueue->BindingClientEntry);
    if (!NT_SUCCESS(Status)) {
        goto Exit;
    }

    *NewRxQueue = RxQueue;
    Status = STATUS_SUCCESS;

Exit:

    TraceInfo(
        TRACE_CORE,
        "RxQueue=%p Hook={%!HOOK_LAYER!, %!HOOK_DIR!, %!HOOK_SUBLAYER!} Status=%!STATUS!",
        RxQueue, HookId->Layer, HookId->Direction, HookId->SubLayer, Status);

    if (!NT_SUCCESS(Status)) {
        if (RxQueue != NULL) {
            XdpRxQueueDereference(RxQueue);
        }
    }

    return Status;
}

XDP_RX_QUEUE *
XdpRxQueueFind(
    _In_ XDP_BINDING_HANDLE Binding,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId
    )
{
    XDP_RX_QUEUE_KEY Key;
    XDP_BINDING_CLIENT_ENTRY *ClientEntry;
    XDP_RX_QUEUE *RxQueue;

    XdpRxQueueInitializeKey(&Key, HookId, QueueId);
    ClientEntry = XdpIfFindClientEntry(Binding, &RxQueueBindingClient, &Key);
    if (ClientEntry == NULL) {
        return NULL;
    }

    RxQueue = XdpRxQueueFromBindingEntry(ClientEntry);
    XdpRxQueueReference(RxQueue);

    return RxQueue;
}

NTSTATUS
XdpRxQueueFindOrCreate(
    _In_ XDP_BINDING_HANDLE Binding,
    _In_ CONST XDP_HOOK_ID *HookId,
    _In_ UINT32 QueueId,
    _Out_ XDP_RX_QUEUE **RxQueue
    )
{
    *RxQueue = XdpRxQueueFind(Binding, HookId, QueueId);
    if (*RxQueue != NULL) {
        return STATUS_SUCCESS;
    }

    return XdpRxQueueCreate(Binding, HookId, QueueId, RxQueue);
}

VOID
XdpRxQueueRegisterNotifications(
    _In_ XDP_RX_QUEUE *RxQueue,
    _Inout_ XDP_RX_QUEUE_NOTIFICATION_ENTRY *NotifyEntry,
    _In_ XDP_RX_QUEUE_NOTIFY *NotifyRoutine
    )
{
    NotifyEntry->NotifyRoutine = NotifyRoutine;
    InsertTailList(&RxQueue->NotifyClients, &NotifyEntry->Link);

    if (RxQueue->InterfaceRxQueue != NULL) {
        NotifyRoutine(NotifyEntry, XDP_RX_QUEUE_NOTIFICATION_ATTACH);
    }
}

VOID
XdpRxQueueDeregisterNotifications(
    _In_ XDP_RX_QUEUE *RxQueue,
    _Inout_ XDP_RX_QUEUE_NOTIFICATION_ENTRY *NotifyEntry
    )
{
    UNREFERENCED_PARAMETER(RxQueue);

    RemoveEntryList(&NotifyEntry->Link);
}

VOID
XdpRxQueueSync(
    _In_ XDP_RX_QUEUE *RxQueue,
    _In_ XDP_QUEUE_SYNC_CALLBACK *Callback,
    _In_opt_ VOID *CallbackContext
    )
{
    XDP_QUEUE_BLOCKING_SYNC_CONTEXT SyncEntry = {0};
    XDP_NOTIFY_QUEUE_FLAGS NotifyFlags = XDP_NOTIFY_QUEUE_FLAG_RX_FLUSH;

    //
    // Serialize a callback with the datapath execution context. This routine
    // must be called from the interface binding thread.
    //

    if (RxQueue->State != XdpRxQueueStateActive) {
        //
        // If the RX queue is not active (i.e. the XDP data path cannot be
        // invoked by interfaces), simply invoke the callback.
        //
        Callback(CallbackContext);
        return;
    }

    XdpQueueBlockingSyncInsert(&RxQueue->Sync, &SyncEntry, Callback, CallbackContext);

    XdbgNotifyQueueEc(RxQueue, NotifyFlags);
    RxQueue->InterfaceRxDispatch->InterfaceNotifyQueue(RxQueue->InterfaceRxQueue, NotifyFlags);

    KeWaitForSingleObject(&SyncEntry.Event, Executive, KernelMode, FALSE, NULL);
}

NTSTATUS
XdpRxQueueSetProgram(
    _In_ XDP_RX_QUEUE *RxQueue,
    _In_opt_ XDP_PROGRAM *Program
    )
{
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT((RxQueue->Program == NULL) != (Program == NULL));

    if (Program != NULL) {
        RxQueue->Program = Program;
        Status = XdpRxQueueAttachInterface(RxQueue);
        if (!NT_SUCCESS(Status)) {
            RxQueue->Program = NULL;
        }
    } else {
        XdpRxQueueDetachInterface(RxQueue);
        RxQueue->Program = NULL;
    }

    return Status;
}

XDP_PROGRAM *
XdpRxQueueGetProgram(
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    return RxQueue->Program;
}

NDIS_HANDLE
XdpRxQueueGetInterfacePollHandle(
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    return RxQueue->InterfaceRxPollHandle;
}

XDP_RX_QUEUE_CONFIG_ACTIVATE
XdpRxQueueGetConfig(
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    return (XDP_RX_QUEUE_CONFIG_ACTIVATE)&RxQueue->ConfigActivate;
}

VOID
XdpRxQueueDereference(
    _In_ XDP_RX_QUEUE *RxQueue
    )
{
    ASSERT(RxQueue->ReferenceCount > 0);
    if (--RxQueue->ReferenceCount == 0) {
        TraceInfo(TRACE_CORE, "Deleting RxQueue=%p", RxQueue);
        XdpIfDeregisterClient(RxQueue->Binding, &RxQueue->BindingClientEntry);
        ExFreePoolWithTag(RxQueue, XDP_POOLTAG_RXQUEUE);
    }
}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
XdpRxRegistryUpdate(
    VOID
    )
{
    NTSTATUS Status;
    DWORD Value;

    Status = XdpRegQueryDwordValue(XDP_PARAMETERS_KEY, L"XdpRxRingSize", &Value);
    if (NT_SUCCESS(Status) && RTL_IS_POWER_OF_TWO(Value) && Value >= 8 && Value <= 8192) {
        XdpRxRingSize = Value;
    }
}

NTSTATUS
XdpRxStart(
    VOID
    )
{
    XdpRxRegistryUpdate();
    XdpRegWatcherAddClient(XdpRegWatcher, XdpRxRegistryUpdate, &XdpRxRegWatcherEntry);
    return STATUS_SUCCESS;
}

VOID
XdpRxStop(
    VOID
    )
{
    XdpRegWatcherRemoveClient(XdpRegWatcher, &XdpRxRegWatcherEntry);
}