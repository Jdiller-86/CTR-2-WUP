/* Original ARM11/Thumb/VFP probe; no commercial instructions or assets. */
.syntax unified
.cpu arm1176jzf-s
.fpu vfp
.arm
.global _start
_start:
    mov r0, #0
    ldr r1, =0x3ff80000
    vmov d0, r0, r1
    ldr r1, =0x3fe00000
    vmov d1, r0, r1
    vadd.f64 d4, d0, d1
    vdiv.f64 d5, d0, d1
    ldr r0, =0x3fc00000
    vmov s0, r0
    ldr r0, =0x3f000000
    vmov s1, r0
    vadd.f32 s2, s0, s1
    vsub.f32 s3, s0, s1
    vmul.f32 s4, s0, s1
    vdiv.f32 s5, s0, s1
    vmov r2, s2
    vcmp.f32 s0, s1
    vmrs APSR_nzcv, fpscr
    mov r0, #0
    adc r0, r0, #0
    /* Literal pointers are a supported upstream discovery source. */
    ldr r7, =(0x100000 + thumb_entry - _start + 1)
    bx r7
.thumb
thumb_entry:
    movs r3, #7
    lsls r3, r3, #1
    adds r3, #3
    push {r2, r3}
    movs r2, #0
    movs r3, #0
    pop {r2, r3}
    svc #3
.arm
.ltorg
