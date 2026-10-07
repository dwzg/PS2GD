;
; The music's tick (music.c has the rest of the player and the tables).
;
; music_tick() runs first in every frame, before the physics, and can't
; wait for a quieter frame: it is assembly, each channel's state at fixed
; addresses (m_ch[c], 16 bytes, music.c's Chan), each channel's code its
; own (macros). About 3 scanlines a tick, 8 when every channel starts a
; note (SDCC's C took 10 and 32).
;
; Channel 3 plays the bass and the kick's falling sine, two waveforms. Its
; wave RAM can only be written while the channel is stopped, and switching
; its DAC off for that clicks on the hardware: so a tick before a kick (and
; on the kick's last tick, for the bass after it) the channel is stopped
; by its length counter instead, and the waveform goes in on the next tick
; with the DAC on.
;
	.module music_asm

	.globl _music_tick, _m_bass_on, _m_kick_peek
	.globl _sfx_tick, _music_beat, _music_bar_beat
	.globl _m_ch, _m_playing, _m_paused, _m_bank, _m_bpm, _m_beat_n, _m_beat_acc
	.globl _m_kick_t, _m_kick_soft, _m_kick_next, _m_wave, _m_wave_dac, _m_nr51
	.globl _m_inst_duty, _m_inst_env, _m_inst_env2, _m_inst_vib, _m_inst_pan, _m_waves
	.globl _m_drum_nr41, _m_drum_env, _m_drum_poly, _m_kick_freq, _m_kick_level, _m_vib_by_depth
	.globl _gbc_freq, __current_bank

; sound registers (0xff00 + n)
NR10 = 0x10
NR31 = 0x1b
NR32 = 0x1c
NR33 = 0x1d
NR34 = 0x1e
NR41 = 0x20
NR42 = 0x21
NR43 = 0x22
NR44 = 0x23
NR51 = 0x25
NR52 = 0x26

; Chan (music.c)
WAIT = 0
P = 2
NOTE = 4
AGE = 5
INST = 6
FREQ = 7
VIB = 9
LOOP = 11
SFX = 13

; musicdata.h
MS_INST = 0xf0
MS_LONGWAIT = 0xf1
GI_WAVE_SAW = 8
DR_KICK = 1
DR_KICK_SOFT = 2
DR_HAT = 7
DR_COUNT = 10
WAVE_KICK = 3
KICK_TICKS = 8
; the length counter's 3 last steps (1/256 s each): channel 3 stops 8 to
; 12 ms after it is set
LEN_STOP = 256 - 3

	.area _CODE

; a = table[a] (hl = its address)
	.macro LDTBL table
	add a, #<table
	ld l, a
	ld a, #0
	adc a, #>table
	ld h, a
	ld a, (hl)
	.endm

; One channel's tick: count its wait down; at 0 read its next events
; (step); then what a sounding note does every tick (update, if upd).
	.macro CHANNEL base, step, upd, update, ?dostep, ?doupd
	ld hl, #base+WAIT
	ld a, (hl+)
	or a, (hl)
	jr z, dostep
	dec hl
	ld a, (hl)
	sub a, #1
	ld (hl+), a
	ld a, (hl)
	sbc a, #0
	ld (hl-), a
	or a, (hl)
	jr nz, doupd
dostep:
	call step
doupd:
	.if upd
	call update
	.endif
	.endm

; A channel's events up to its next wait: notes (noteon), note offs (off),
; instruments, the end of the stream. Then, if peek, on to peek (which
; looks at what the next events start, for the kick).
	.macro STEP base, noteon, off, peek, ?next, ?high, ?inst, ?long, ?short, ?store, ?offl
	ld hl, #base+P
	ld a, (hl+)
	ld d, (hl)
	ld e, a
next:
	ld a, (de)
	inc de
	cp a, #0x80
	jr nc, high
	or a, a
	jr z, offl
	ld (base+NOTE), a
	xor a, a
	ld (base+AGE), a
	push de
	call noteon
	pop de
	jr next
offl:
	ld (base+NOTE), a
	push de
	call off
	pop de
	jr next
high:
	cp a, #MS_INST
	jr c, short
	jr z, inst
	cp a, #MS_LONGWAIT
	jr z, long
	; the end: on from the loop point
	ld hl, #base+LOOP
	ld a, (hl+)
	ld d, (hl)
	ld e, a
	jr next
inst:
	ld a, (de)
	inc de
	ld (base+INST), a
	jr next
long:
	ld a, (de)
	inc de
	ld (base+WAIT), a
	ld a, (de)
	inc de
	ld (base+WAIT+1), a
	jr store
short:
	sub a, #0x7f
	ld (base+WAIT), a
	xor a, a
	ld (base+WAIT+1), a
store:
	ld hl, #base+P
	ld a, e
	ld (hl+), a
	ld (hl), d
	.if peek
	jp kick_peek
	.else
	ret
	.endif
	.endm

; A pulse channel's note (NR registers from nr, envelopes from envs): its
; frequency and vibrato are kept (also while a sound effect has the
; channel), then it starts. Channel 2 (pan) is panned by its instrument.
	.macro NOTEON_PULSE base, nr, pan, envs, ?novib, ?vibdone, ?samepan
	ld a, (base+NOTE)
	call freq_of
	ld hl, #base+FREQ
	ld a, e
	ld (hl+), a
	ld (hl), d
	ld a, (base+INST)
	ld c, a
	LDTBL _m_inst_vib
	or a, a
	jr z, novib
	; the row for depth ((0x800 - f) >> 7) & 15
	xor a, a
	sub a, e
	ld l, a
	ld a, #0x08
	sbc a, d
	sla l
	rla
	and a, #15
	add a, a
	add a, a
	add a, a
	add a, #<_m_vib_by_depth
	ld l, a
	ld a, #0
	adc a, #>_m_vib_by_depth
	ld h, a
	jr vibdone
novib:
	ld h, a
	ld l, a
vibdone:
	ld a, l
	ld (base+VIB), a
	ld a, h
	ld (base+VIB+1), a
	ld a, (base+SFX)
	or a, a
	ret nz
	.if pan
	ld a, c
	LDTBL _m_inst_pan
	ld b, a
	ld hl, #_m_nr51
	ld a, (hl)
	and a, #0xdd
	or a, b
	cp a, (hl)
	jr z, samepan
	ld (hl), a
	ldh (NR51), a
samepan:
	.else
	xor a, a
	ldh (NR10), a
	.endif
	ld a, c
	LDTBL _m_inst_duty
	ldh (nr+1), a
	ld a, c
	LDTBL envs
	ldh (nr+2), a
	ld a, e
	ldh (nr+3), a
	ld a, d
	or a, #0x80
	ldh (nr+4), a
	ret
	.endm

; A pulse channel's note off: restarted at volume 0 (its DAC stays on).
	.macro OFF_PULSE base, nr
	ld a, (base+SFX)
	or a, a
	ret nz
	ld a, #0x08
	ldh (nr+2), a
	ld a, #0x80
	ldh (nr+4), a
	ret
	.endm

; Every tick of a pulse channel's note: its age, and past 12 ticks the
; vibrato.
	.macro UPDATE_PULSE base, nr, ?keep
	ld a, (base+NOTE)
	or a, a
	ret z
	ld hl, #base+AGE
	ld a, (hl)
	inc a
	jr z, keep
	ld (hl), a
keep:
	ld a, (base+SFX)
	or a, a
	ret nz
	ld a, (hl)
	cp a, #13
	ret c
	ld c, a
	ld hl, #base+VIB
	ld a, (hl+)
	ld h, (hl)
	ld l, a
	or a, h
	ret z
	ld a, c
	and a, #7
	add a, l
	ld l, a
	adc a, h
	sub a, l
	ld h, a
	ld a, (hl)
	ld e, a
	rlca
	sbc a, a
	ld d, a
	ld hl, #base+FREQ
	ld a, (hl+)
	ld h, (hl)
	ld l, a
	add hl, de
	ld a, l
	ldh (nr+3), a
	ld a, h
	ldh (nr+4), a
	ret
	.endm

CH1 = 0
CH2 = 16
CH3 = 32
CH4 = 48

; --- the tick ---

_music_tick::
	xor a, a
	ld (_music_beat), a
	call _sfx_tick
	ld a, (_m_playing)
	or a, a
	ret z
	ld a, (_m_paused)
	or a, a
	ret nz
	; a beat every 3600 / bpm ticks
	ld a, (_m_bpm)
	ld c, a
	ld b, #0
	ld hl, #_m_beat_acc
	ld a, (hl+)
	ld h, (hl)
	ld l, a
	ld a, h
	or a, a
	jr nz, 1$
	ld a, l
	cp a, c
	jr nc, 1$
	ld a, #1
	ld (_music_beat), a
	ld a, (_m_beat_n)
	ld (_music_bar_beat), a
	inc a
	and a, #3
	ld (_m_beat_n), a
1$:
	add hl, bc
	ld a, l
	sub a, #0x10 ; 3600
	ld e, a
	ld a, h
	sbc a, #0x0e
	jr c, 2$
	ld h, a
	ld l, e
2$:
	ld a, l
	ld (_m_beat_acc), a
	ld a, h
	ld (_m_beat_acc+1), a
	; the song's bank
	ldh a, (__current_bank)
	push af
	ld a, (_m_bank)
	ldh (__current_bank), a
	ld (#0x2000), a

	CHANNEL _m_ch+CH1, step1, 1, update1
	CHANNEL _m_ch+CH2, step2, 1, update2
	CHANNEL _m_ch+CH3, step3, 0, 0
	CHANNEL _m_ch+CH4, step4, 1, update4
	ld a, (_m_kick_t)
	or a, a
	call nz, kick_tick
	; A kick on the next tick: channel 3 is stopped by its length counter
	; 8 to 12 ms from now, before the next tick (17 ms on), and the kick's
	; waveform goes in then with the DAC on.
	ld a, (_m_wave)
	cp a, #WAVE_KICK
	jr z, 3$
	call kick_due
	jr z, 3$
	ld a, #LEN_STOP
	ldh (NR31), a
	ld a, (_m_ch+CH3+FREQ+1)
	or a, #0x40
	ldh (NR34), a
3$:
	pop af
	ldh (__current_bank), a
	ld (#0x2000), a
	ret

; --- channel 1: the lead ---
step1:
	STEP _m_ch+CH1, noteon1, off1, 0
noteon1:
	NOTEON_PULSE _m_ch+CH1, 0x10, 0, _m_inst_env
off1:
	OFF_PULSE _m_ch+CH1, 0x10
update1:
	UPDATE_PULSE _m_ch+CH1, 0x10

; --- channel 2: the arpeggio ---
step2:
	STEP _m_ch+CH2, noteon2, off2, 0
noteon2:
	NOTEON_PULSE _m_ch+CH2, 0x15, 1, _m_inst_env2
off2:
	OFF_PULSE _m_ch+CH2, 0x15
update2:
	UPDATE_PULSE _m_ch+CH2, 0x15

; --- channel 3: the bass, and the kick's tone ---
step3:
	STEP _m_ch+CH3, _m_bass_on, off3, 0

; Channel 3's note, unless the kick has the channel or takes it this tick
; (the bass comes back after it).
_m_bass_on::
	ld a, (_m_kick_t)
	or a, a
	ret nz
	call kick_due
	ret nz
	ld a, (_m_ch+CH3+NOTE)
	add a, #12 ; (the wave channel plays an octave below the pulse channels' register value)
	call freq_of
	ld hl, #_m_ch+CH3+FREQ
	ld a, e
	ld (hl+), a
	ld (hl), d
	ld a, (_m_ch+CH3+INST)
	sub a, #GI_WAVE_SAW
	jr nc, 1$
	xor a, a
1$:
	push de
	call wave_load
	pop de
	ld a, #0x20
	ldh (NR32), a
	ld a, e
	ldh (NR33), a
	ld a, d
	or a, #0x80
	ldh (NR34), a
	ret

; muted, and stopped by the length counter (music.c's ch_off)
off3:
	ld a, (_m_kick_t)
	or a, a
	ret nz
	ldh (NR32), a
	dec a
	ldh (NR31), a
	ld a, (_m_ch+CH3+FREQ+1)
	or a, #0x40
	ldh (NR34), a
	ret

; Waveform a into wave RAM, if it isn't there. The channel is stopped
; then (by its length counter, a tick before): its wave RAM can be written
; with the DAC on. Turning the DAC off and on clicks on the hardware; only
; a waveform changed while a note still plays needs it (m_wave_dac counts
; those, for the emulator test).
wave_load:
	ld hl, #_m_wave
	cp a, (hl)
	ret z
	ld (hl), a
	swap a
	add a, #<_m_waves
	ld l, a
	ld a, #0
	adc a, #>_m_waves
	ld h, a
	ldh a, (NR52)
	and a, #4
	jr z, 2$
	xor a, a
	ldh (0x1a), a ; NR30
	call 2$
	ld a, #0x80
	ldh (0x1a), a
	ld hl, #_m_wave_dac
	inc (hl)
	ret
2$:
	ld bc, #0x1030
3$:
	ld a, (hl+)
	ldh (c), a
	inc c
	dec b
	jr nz, 3$
	ret

; A tick of the kick on channel 3; after it, the bass's note again.
kick_tick:
	dec a
	cp a, #KICK_TICKS
	jr c, 1$
	xor a, a
	ld (_m_kick_t), a
	ld a, (_m_ch+CH3+NOTE)
	or a, a
	jp nz, _m_bass_on
	ldh (NR32), a
	ret
1$:
	ld b, a ; the kick's tick
	ld a, (_m_kick_soft)
	add a, a
	add a, a
	add a, a
	add a, b
	ld c, a ; its row and tick
	add a, a
	add a, #<_m_kick_freq
	ld l, a
	ld a, #0
	adc a, #>_m_kick_freq
	ld h, a
	ld a, (hl+)
	ld e, a
	ld d, (hl)
	ld a, b
	or a, a
	jr nz, 2$
	push bc
	push de
	ld a, #WAVE_KICK
	call wave_load
	pop de
	pop bc
2$:
	ld a, c
	LDTBL _m_kick_level
	ldh (NR32), a
	ld a, e
	ldh (NR33), a
	ld a, b
	or a, a
	jr nz, 3$
	ld a, d
	or a, #0x80 ; started
	jr 5$
3$:
	cp a, #KICK_TICKS-1
	ld a, d
	jr nz, 5$
	; its last tick: stopped by the length counter before the next, so
	; that the bass's waveform goes in with the DAC on
	ld a, #LEN_STOP
	ldh (NR31), a
	ld a, d
	or a, #0x40
5$:
	ldh (NR34), a
	ld hl, #_m_kick_t
	inc (hl)
	ret

; Is a kick due on channel 4 on this tick (before channel 4 is read) or
; the next (after)? NZ if so.
kick_due:
	ld a, (_m_kick_next)
	or a, a
	ret z
	ld a, (_m_ch+CH4+WAIT+1)
	or a, a
	jr nz, 1$
	ld a, (_m_ch+CH4+WAIT)
	cp a, #2
	jr nc, 1$
	ld a, (_m_ch+CH4+SFX)
	or a, a
	jr nz, 1$
	inc a
	ret
1$:
	xor a, a
	ret

; --- channel 4: the drums ---
step4:
	STEP _m_ch+CH4, noteon4, off4, 1

noteon4:
	ld a, (_m_ch+CH4+SFX)
	or a, a
	ret nz
	ld a, (_m_ch+CH4+NOTE)
	cp a, #DR_COUNT
	jr c, 1$
	ld a, #DR_HAT
1$:
	ld c, a
	cp a, #DR_KICK
	jr z, 2$
	cp a, #DR_KICK_SOFT
	jr nz, 3$
2$:
	; a kick: its tone on channel 3
	sub a, #DR_KICK
	ld (_m_kick_soft), a
	ld a, #1
	ld (_m_kick_t), a
3$:
	ld a, c
	LDTBL _m_drum_nr41
	ldh (NR41), a
	ld a, c
	LDTBL _m_drum_env
	ldh (NR42), a
	ld a, c
	add a, a
	add a, c
	LDTBL _m_drum_poly
	ldh (NR43), a
	ld a, #0xc0 ; with the length counter
	ldh (NR44), a
	ret

off4:
	ld a, (_m_ch+CH4+SFX)
	or a, a
	ret nz
	ld a, #0x08
	ldh (NR42), a
	ld a, #0x80
	ldh (NR44), a
	ret

; the drum's noise setting for its first three ticks
update4:
	ld a, (_m_ch+CH4+NOTE)
	or a, a
	ret z
	ld c, a
	ld hl, #_m_ch+CH4+AGE
	ld a, (hl)
	ld b, a
	inc a
	jr z, 1$
	ld (hl), a
1$:
	ld a, (_m_ch+CH4+SFX)
	or a, a
	ret nz
	ld a, b
	or a, a
	ret z
	cp a, #3
	ret nc
	ld a, c
	cp a, #DR_COUNT
	jr c, 2$
	ld a, #DR_HAT
2$:
	ld c, a
	add a, a
	add a, c
	add a, b
	LDTBL _m_drum_poly
	ldh (NR43), a
	ret

; Is channel 4's next note (from its stream position, de) a kick? For
; channel 3, which has to be stopped a tick before (above).
_m_kick_peek::
	ld hl, #_m_ch+CH4+P
	ld a, (hl+)
	ld d, (hl)
	ld e, a
kick_peek:
	ld a, (de)
	inc de
	cp a, #MS_INST
	jr nz, 1$
	inc de
	jr kick_peek
1$:
	cp a, #0xff
	jr nz, 2$
	ld hl, #_m_ch+CH4+LOOP
	ld a, (hl+)
	ld d, (hl)
	ld e, a
	jr kick_peek
2$:
	or a, a
	jr z, kick_peek
	cp a, #DR_KICK
	jr z, 3$
	cp a, #DR_KICK_SOFT
	jr z, 3$
	xor a, a
	ld (_m_kick_next), a
	ret
3$:
	ld a, #1
	ld (_m_kick_next), a
	ret

; de = the frequency register value of note a (MIDI, clamped to 36..119)
freq_of:
	cp a, #36
	jr nc, 1$
	ld a, #36
1$:
	cp a, #120
	jr c, 2$
	ld a, #119
2$:
	sub a, #36
	add a, a
	add a, #<_gbc_freq
	ld l, a
	ld a, #0
	adc a, #>_gbc_freq
	ld h, a
	ld a, (hl+)
	ld e, a
	ld d, (hl)
	ret
