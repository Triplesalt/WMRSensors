INCLUDE Hook.inc

_text SEGMENT

_Hook_OpenCameraStream PROC
	;RCX : camera type (1-4)
	;RDX : callback
	;R8 : user data
	;R9 : handle output pointer
	POP RAX
	PUSH R9
	SUB RSP, 20h

	LEA R10, [_PostOpenCameraStream]
	PUSH R10
	;Backup
	PUSH RBP
	PUSH RBX
	PUSH RSI
	PUSH RDI
	PUSH R12
	PUSH R13
	PUSH R14
	PUSH R15
	JMP RAX
	
	_PostOpenCameraStream:
	ADD RSP, 20h
	POP RCX ;handle output pointer
	TEST EAX, EAX
	JS _PostOpenCameraStream_Failed

	PUSH RAX
	SUB RSP, 20h
	MOV RCX, QWORD PTR [RCX]
	CALL _OnOpenCameraStream
	ADD RSP, 20h
	POP RAX

	_PostOpenCameraStream_Failed:
	RET
_Hook_OpenCameraStream ENDP

_Hook_CloseCameraStream PROC
	;RCX : Camera stream client object
	PUSH RCX
	PUSH RDX
	PUSH R8
	PUSH R9

	SUB RSP, 20h
	
	CALL _OnCloseCameraStream

	ADD RSP, 20h

	POP R9
	POP R8
	POP RDX
	POP RCX

	POP RAX ;Return IP
	
	;Backup
	MOV QWORD PTR [RSP+10h], RBX
	PUSH RBP
	
	MOV RBX, QWORD PTR [HookCloseCameraStream_RBPOffset] ;Custom
	MOV RBP, RSP
	ADD RBP, RBX
	
	MOVSXD RBX, DWORD PTR [HookCloseCameraStream_StackSize] ;Custom
	SUB RSP, RBX

	JMP RAX
_Hook_CloseCameraStream ENDP

_Hook_StartCameraStream PROC
	;RCX : Camera stream client object
	PUSH RCX
	PUSH RDX
	PUSH R8
	PUSH R9

	SUB RSP, 20h
	
	CALL _OnStartCameraStream

	ADD RSP, 20h

	POP R9
	POP R8
	POP RDX
	POP RCX

	POP RAX ;Return IP
	
	;Backup
	MOV QWORD PTR [RSP+8], RBX
	PUSH RDI

	MOV EBX, DWORD PTR [HookStartCameraStream_RSPSubOffs] ;Custom
	SUB RSP, RBX
	
	XOR EBX, EBX
	JMP RAX
_Hook_StartCameraStream ENDP

_Hook_StopCameraStream PROC
	;RCX : NOTed camera pair ID
	PUSH RCX
	PUSH RDX
	PUSH R8
	PUSH R9

	SUB RSP, 20h
	
	CALL _OnStopCameraStream

	ADD RSP, 20h

	POP R9
	POP R8
	POP RDX
	POP RCX

	POP RAX ;Return IP
	
	;Backup
	MOV QWORD PTR [RSP+10h], RBX
	MOV QWORD PTR [RSP+18h], RSI
	PUSH RBP
	PUSH RDI

	JMP RAX
_Hook_StopCameraStream ENDP

_Hook_OpenIMUStream PROC
	POP RAX
	PUSH RCX
	PUSH RDX
	SUB RSP, 28h

	LEA R8, [_PostOpenIMUStream]
	PUSH R8 ;Return address
	;Backup
	MOV QWORD PTR [RSP+18h], RBX
	PUSH RBP
	PUSH RSI
	PUSH RDI
	MOV R8, QWORD PTR [HookOpenIMUStream_RBPOffset]
	MOV RBP, RSP
	ADD RBP, R8
	JMP RAX
	
	_PostOpenIMUStream:
	MOV QWORD PTR [RSP+20h], RAX
	TEST EAX, EAX
	JS _FailOpenIMUStream

	MOV RCX, QWORD PTR [RSP+30h]
	MOV RDX, QWORD PTR [RSP+28h]
	CALL _OnOpenIMUStream
	
	_FailOpenIMUStream:
	ADD RSP, 20h
	POP RAX
	POP RDX
	POP RCX
	RET
_Hook_OpenIMUStream ENDP

_Hook_CloseIMUStream PROC
	POP RAX
	SUB RSP, 28h

	LEA R8, [_PostCloseIMUStream]
	PUSH R8 ;Return address
	;Backup
	MOV QWORD PTR [RSP+8h], RBX
	PUSH RBP
	MOV R8, QWORD PTR [HookCloseIMUStream_RBPOffset]
	MOV RBP, RSP
	ADD RBP, R8
	MOVSXD R8, DWORD PTR [HookCloseIMUStream_RSPSubOffs]
	SUB RSP, R8
	JMP RAX
	
	_PostCloseIMUStream:
	MOV QWORD PTR [RSP+20h], RAX
	TEST EAX, EAX
	JS _FailCloseIMUStream

	CALL _OnCloseIMUStream
	
	_FailCloseIMUStream:
	ADD RSP, 20h
	POP RAX
	RET
_Hook_CloseIMUStream ENDP

_Hook_CrystalKeyStartIMUStream PROC
	;RCX : controller handle
	;RDX : callback
	;R8 : user data
	PUSH RCX
	PUSH RDX
	PUSH R8

	SUB RSP, 38h

	LEA RAX, [RSP+20h] ; parameter pCallback
	MOV QWORD PTR [RAX], RDX
	MOV RDX, RAX

	LEA RAX, [RSP+28h] ; parameter pUserData
	MOV QWORD PTR [RAX], R8
	MOV R8, RAX

	CALL _OnCrystalKeyStartIMUStream

	MOV RCX, [RSP+48h]
	MOV RDX, [RSP+20h]
	MOV R8, [RSP+28h]
	MOV RAX, [RSP+50h]

	; Call CrystalKeyStartIMUStream
	; Backup
	LEA R10, [_PostCrystalKeyStartIMUStream]
	PUSH R10 ;Address after the Hook CALL
	MOV QWORD PTR [RSP+8h], RBX
	MOV QWORD PTR [RSP+10h], RSI
	PUSH RDI
	MOV R10D, [HookCrystalKeyStartIMUStream_RSPSubOffs]
	SUB RSP, R10

	JMP RAX
	_PostCrystalKeyStartIMUStream:
	
	TEST EAX, EAX
	JS _FailCrystalKeyStartIMUStream

	MOV QWORD PTR [RSP+20h], RAX

	MOV RCX, [RSP+48h]
	MOV RDX, [RSP+40h]
	MOV R8, [RSP+38h]
	CALL _OnSuccessCrystalKeyStartIMUStream

	MOV RAX, QWORD PTR [RSP+20h]

	_FailCrystalKeyStartIMUStream:

	ADD RSP, 38h
	POP R8
	POP RDX
	POP RCX
	POP R10
	RET
_Hook_CrystalKeyStartIMUStream ENDP

_Hook_CrystalKeyStopIMUStream PROC
	;RCX : controller handle
	PUSH RCX
	SUB RSP, 28h
	CALL _OnPostCrystalKeyStopIMUStream
	ADD RSP, 28h
	POP RCX

	POP RAX ;Return IP
	;Backup
	MOV QWORD PTR [RSP+8h], RBX
	PUSH RDI
	MOVSXD R10, DWORD PTR [HookCrystalKeyStopIMUStream_RSPSubOffs]
	SUB RSP, R10
	MOV R10, QWORD PTR [HookCrystalKeyStopIMUStream_CMPAddr]
	CMP DWORD PTR [R10], 0
	JMP RAX
_Hook_CrystalKeyStopIMUStream ENDP

_Hook_ControllerStateTransition PROC
	;The volatile registers RAX, RCX, RDX, R8, R10, R11 can be overwritten, only R9 is required by the caller.
	MOV RCX, RBX ;pDriftManager
	MOV RDX, R9 ;oldStateName
	MOV R8, QWORD PTR [RSP+28h] ;newStateName
	PUSH R9
	SUB RSP, 20h
	CALL _OnControllerStateTransition
	ADD RSP, 20h
	POP R9

	;Backup
	MOV RDX, QWORD PTR [HookControllerStateTransition_FormatString]
	MOV RCX, QWORD PTR [HookControllerStateTransition_ModuleNameString]
	RET
_Hook_ControllerStateTransition ENDP

_text ENDS
END