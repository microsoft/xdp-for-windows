TARGETNAME=xdpmp
TARGETTYPE=DRIVER
TARGET_DESTINATION=xdp\test\xdpmp

NT_SIGNCODE=1
TEST_CODE=1

C_DEFINES=\
    $(C_DEFINES) \
    -DNDIS_MINIPORT_DRIVER \
    -DNDIS685_MINIPORT=1 \

MSC_WARNING_LEVEL=/W4

TARGETLIBS=\
    $(ONECORE_EXTERNAL_DDK_LIB_PATH)\ndis.lib \
    $(ONECORE_EXTERNAL_DDK_LIB_PATH)\netio.lib \
    $(XDP_OBJ_ROOT)\lib\nmr\$(O)\xdpnmr.lib \

INCLUDES=\
    ..; \
    $(ONECORE_EXTERNAL_DDK_INC_PATH); \
    $(XDP_DDK_INC); \
    $(XDP_ROOT)\lib\rtl\inc; \
    $(XDP_ROOT)\test\pkthlp; \

SOURCES=\
    ..\hwring.c \
    ..\miniport.c \
    ..\pace.c \
    ..\rss.c \
    ..\rx.c \
    ..\tx.c \
    ..\xdpmp.mof \
    ..\xdpmp.rc \

NTTARGETFILE0=\
    $(OBJ_PATH)\$(O)\xdpmpwmi.h
