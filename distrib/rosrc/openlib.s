;**************************************
;	openlib.s
;	source de openlib.ro
;**************************************
OpenLibrary	EQU	-552

	move.l	4.w,a6
; 2�me param�tre: version minimale de la biblioth�que
	move.l	4(a1),d0
; 1er param�tre: pointeur sur le nom de la biblioth�que
	move.l	(a1),a1
	jsr	OpenLibrary(a6)
	rts			;r�sultat dans d0
