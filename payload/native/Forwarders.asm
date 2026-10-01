option casemap:none
EXTERN NSFG_GetSystemExport:PROC
.code
NSFGProxy_ApplyCompatResolutionQuirking PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 0
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_ApplyCompatResolutionQuirking ENDP
NSFGProxy_CompatString PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 1
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_CompatString ENDP
NSFGProxy_CompatValue PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 2
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_CompatValue ENDP
NSFGProxy_DXGIDumpJournal PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 3
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_DXGIDumpJournal ENDP
NSFGProxy_PIXBeginCapture PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 4
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_PIXBeginCapture ENDP
NSFGProxy_PIXEndCapture PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 5
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_PIXEndCapture ENDP
NSFGProxy_PIXGetCaptureState PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 6
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_PIXGetCaptureState ENDP
NSFGProxy_SetAppCompatStringPointer PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 7
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_SetAppCompatStringPointer ENDP
NSFGProxy_UpdateHMDEmulationStatus PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 8
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_UpdateHMDEmulationStatus ENDP
NSFGProxy_DXGID3D10CreateDevice PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 12
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_DXGID3D10CreateDevice ENDP
NSFGProxy_DXGID3D10CreateLayeredDevice PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 13
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_DXGID3D10CreateLayeredDevice ENDP
NSFGProxy_DXGID3D10GetLayeredDeviceSize PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 14
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_DXGID3D10GetLayeredDeviceSize ENDP
NSFGProxy_DXGID3D10RegisterLayers PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 15
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_DXGID3D10RegisterLayers ENDP
NSFGProxy_DXGIDeclareAdapterRemovalSupport PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 16
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_DXGIDeclareAdapterRemovalSupport ENDP
NSFGProxy_DXGIDisableVBlankVirtualization PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 17
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_DXGIDisableVBlankVirtualization ENDP
NSFGProxy_DXGIGetDebugInterface1 PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 18
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_DXGIGetDebugInterface1 ENDP
NSFGProxy_DXGIReportAdapterConfiguration PROC FRAME
    sub rsp, 088h
    .allocstack 088h
    .endprolog
    mov [rsp+020h], rcx
    mov [rsp+028h], rdx
    mov [rsp+030h], r8
    mov [rsp+038h], r9
    movdqu [rsp+040h], xmm0
    movdqu [rsp+050h], xmm1
    movdqu [rsp+060h], xmm2
    movdqu [rsp+070h], xmm3
    mov ecx, 19
    call NSFG_GetSystemExport
    mov rcx, [rsp+020h]
    mov rdx, [rsp+028h]
    mov r8, [rsp+030h]
    mov r9, [rsp+038h]
    movdqu xmm0, [rsp+040h]
    movdqu xmm1, [rsp+050h]
    movdqu xmm2, [rsp+060h]
    movdqu xmm3, [rsp+070h]
    add rsp, 088h
    jmp rax
NSFGProxy_DXGIReportAdapterConfiguration ENDP
END
