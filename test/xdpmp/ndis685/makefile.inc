clean:
    $(DEL) $(OBJ_PATH)\$(O)\xdpmpwmi.h $(OBJ_PATH)\$(O)\xdpmp.bmf

$(OBJ_PATH)\$(O)\xdpmp.mof: ..\xdpmp.mof

$(OBJ_PATH)\$(O)\xdpmpwmi.h: $(OBJ_PATH)\$(O)\xdpmp.bmf
    $(WMIMOFCK) -h$(OBJ_PATH)\$(O)\xdpmpwmi.h $(OBJ_PATH)\$(O)\xdpmp.bmf
