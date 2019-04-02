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
	
	MOV EBX, DWORD PTR [HookCloseCameraStream_StackSize] ;Custom
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

_text ENDS
END