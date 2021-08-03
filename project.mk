!include $(BASEDIR)\onecore\net\project.mk

XDP_ROOT=$(PROJECT_ROOT)\xdp
XDP_OBJ_ROOT=$(PROJECT_OBJ_ROOT)\xdp

XDP_MAJOR_VER=0
XDP_MINOR_VER=9
XDP_PATCH_VER=2

#
# Define POOL_NX_OPTIN_AUTO to use non executable pool.
#
C_DEFINES=$(C_DEFINES) -DPOOL_NX_OPTIN_AUTO=1 -DPOOL_ZERO_DOWN_LEVEL_SUPPORT=1

#
# Disable FNDIS dependencies by default.
# FNDIS is required for experimental AF_XDP poll APIs and lower CPU overhead.
#
FNDIS=0

#
# To speed up the developer inner loop, use published headers directly instead
# of via the publics system in no_opt razzle. This also enables Intellisense
# across the published boundary.
#
# In regular builds (and official builds) use the publics path to ensure the
# published headers are in fact published.
#

XDP_SDK_INC=\
!ifdef BUILD_NO_OPT
    $(XDP_ROOT)\published\external; \
!else
    $(MINWIN_INTERNAL_PRIV_SDK_INC_PATH_L)\xdp\sdk; \
    $(MINWIN_INTERNAL_PRIV_SDK_INC_PATH_L)\xdp\shared; \
!endif

XDP_DDK_INC=\
!ifdef BUILD_NO_OPT
    $(XDP_ROOT)\published\external; \
!else
    $(MINWIN_INTERNAL_PRIV_SDK_INC_PATH_L)\xdp\ddk; \
    $(MINWIN_INTERNAL_PRIV_SDK_INC_PATH_L)\xdp\shared; \
!endif
