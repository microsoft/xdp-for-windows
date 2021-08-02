#
# Common build macros for a "verified" build flavor, which enables asserts and
# other test code regardless of chk/fre build flavor. Useful for ES test
# automation where only fre binaries are used.
#

TARGET_DESTINATION=xdp\test\xdpvrf

#
# Forces symbols to be binplaced according to
# %_NTTREE%\symbols.pri\[file_extension]\TARGET_DESTINATION.
#
BINPLACE_FLAGS=$(BINPLACE_FLAGS) /:FULLCLASS

C_DEFINES=$(C_DEFINES) -DXDP_VRF=1 -DDBG=1
XDP_VRF=1
