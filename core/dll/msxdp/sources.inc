TARGETNAME=msxdp
TARGETTYPE=DYNLINK
TARGET_DESTINATION=xdp

DLLDEF=$(OBJ_PATH)\$(O)\msxdp.def

MSC_WARNING_LEVEL=/W4 /WX

PRECOMPILED_INCLUDE=..\precomp.h
BUILD_FOR_CORESYSTEM=1
UMTYPE=console
USE_MSVCRT=1
MUI_VERIFY_NO_LOC_RESOURCE=1

C_DEFINES=$(C_DEFINES) /DUNICODE /D_UNICODE

TARGETLIBS= \
    $(ONECORE_EXTERNAL_SDK_LIB_PATH)\ntdll.lib \
    $(ONECORE_EXTERNAL_SDK_LIB_PATH)\onecore.lib \

INCLUDES= \
    ..; \
    $(XDP_ROOT)\published\private; \
    $(XDP_ROOT)\lib\rtl\inc; \
    $(XDP_SDK_INC); \

SOURCES= \
    ..\afxdp.c \
    ..\ioctl.c \
    ..\msxdp.rc \
    ..\msxdp.c \
