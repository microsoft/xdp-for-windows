TARGETNAME=xdp
TARGETTYPE=DRIVER
TARGET_DESTINATION=xdp

PRECOMPILED_INCLUDE=..\precomp.h

C_DEFINES=$(C_DEFINES) \
    -DNDIS_WDM=1 \
    -DNDIS684=1 \

MSC_WARNING_LEVEL=/WX /W4

TARGETLIBS=\
    $(ONECORE_EXTERNAL_DDK_LIB_PATH)\ndis.lib \
    $(ONECORE_EXTERNAL_DDK_LIB_PATH)\netio.lib \
    $(ONECORE_EXTERNAL_DDK_LIB_PATH)\wpprecorder.lib \
    $(XDP_OBJ_ROOT)\lib\nmr\$(O)\xdpnmr.lib \
    $(XDP_OBJ_ROOT)\lib\pollshim\$(O)\pollshim.lib \
    $(XDP_OBJ_ROOT)\lib\rtl\$(O)\xdprtl.lib \
    $(XDP_OBJ_ROOT)\lwf\$(O)\xdplwf.lib \

INCLUDES=\
    ..; \
    ..\inc; \
    $(XDP_ROOT)\published\private; \
    $(XDP_ROOT)\lib\pollshim\inc; \
    $(XDP_ROOT)\lib\rtl\inc; \
    $(XDP_ROOT)\lwf\inc; \
    $(ONECORE_EXTERNAL_DDK_INC_PATH); \
    $(ONECORE_EXTERNAL_SHARED_INC_PATH); \
    $(XDP_SDK_INC); \
    $(XDP_DDK_INC); \

SOURCES=\
    ..\bind.c \
    ..\dispatch.c \
    ..\extensionset.c \
    ..\program.c \
    ..\queue.c \
    ..\redirect.c \
    ..\ring.c \
    ..\rx.c \
    ..\tx.c \
    ..\xsk.c \
    ..\xdp.rc \

NO_PDB_PATHS=1

MUI_VERIFY_NO_LOC_RESOURCE=1

NTTARGETFILE0=
INFS_VERSION=$(XDP_MAJOR_VER).$(XDP_MINOR_VER).$(XDP_PATCH_VER).0
GENERIC_INFS=..\xdp.inx
INFS_CATALOGFILE=xdp.cat

RUN_WPP= $(SOURCES) -km -DENABLE_WPP_RECORDER=1 -scan:$(XDP_ROOT)\core\sys\inc\xdptrace.h
