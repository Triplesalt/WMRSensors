INCLUDE Hook.inc

_text SEGMENT

_Hook_StartCameraStream PROC
	;RCX : Camera stream object (?)
	PUSH RCX
	PUSH RDX
	PUSH R8
	PUSH R9
	
	PUSH RBP
	MOV RBP, RSP

	MOV RAX, RSP
	SHR RAX, 4
	SHL RAX, 4
	MOV RSP, RAX

	SUB RSP, 20h
	
	MOV ECX, DWORD PTR [RCX+10h]
	CALL _OnStartCameraStream

	ADD RSP, 20h

	MOV RSP, RBP
	POP RBP

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
	
	PUSH RBP
	MOV RBP, RSP

	MOV RAX, RSP
	SHR RAX, 4
	SHL RAX, 4
	MOV RSP, RAX

	SUB RSP, 20h
	
	MOV ECX, DWORD PTR [RCX+10h]
	CALL _OnStopCameraStream

	ADD RSP, 20h

	MOV RSP, RBP
	POP RBP

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

_Hook_GrabImage PROC
	PUSH RCX
	PUSH RDX
	PUSH R8
	PUSH R9
	PUSH R10
	PUSH R11

	PUSH RBP
	MOV RBP, RSP

	MOV RAX, RSP
	SHR RAX, 4
	SHL RAX, 4
	MOV RSP, RAX

	SUB RSP, 20h
	
	;MOV EDX, EDX ;timestamp
	MOVZX ECX, WORD PTR [R15 + 6] ;camera id
	CALL _OnGrabCameraImage

	ADD RSP, 20h

	MOV RSP, RBP
	POP RBP

	POP R11
	POP R10
	POP R9
	POP R8
	POP RDX
	POP RCX

	;Backup
	POP R8 ;Return IP (points to the middle of an instruction)

	MOV EAX, DWORD PTR [HookGrabImage_RBXBackOffs] ;Custom
	MOV RBX, QWORD PTR [RSP+RAX]
	MOV QWORD PTR [R15+8], RDX
	MOVZX EAX, BYTE PTR [R8+2] ;Custom
	ADD RSP, RAX
	XOR EAX, EAX

	ADD R8, 3
	JMP R8
_Hook_GrabImage ENDP

_text ENDS
END