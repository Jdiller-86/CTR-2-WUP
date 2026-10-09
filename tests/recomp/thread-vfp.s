/* Original per-thread scalar VFP/register/status preservation probe. */
.syntax unified
.cpu arm1176jzf-s
.fpu vfp
.arm
.global _start
_start:
    ldr r8, =0x08000000
    mov r0, #0x40000000
    vmsr fpscr, r0
    mov r0, #0x3f800000
    vmov s0, r0
    mov r0, #0x40000000
    vmov s1, r0
    mov r0, #0x30
    ldr r1, =child
    ldr r2, =0x41200000
    ldr r3, =0x0fffe000
    mvn r4, #1
    svc #8
    mov r9, r1
    mov r0, r9
    mvn r2, #0
    mvn r3, #0
    svc #0x24
    vadd.f32 s2, s0, s1
    vmov r0, s2
    str r0, [r8]
    vmrs r0, fpscr
    str r0, [r8, #4]
    mov r0, r9
    svc #0x23
    svc #3
child:
    vmov s0, r0
    ldr r0, =0x41a00000
    vmov s1, r0
    ldr r8, =0x08000000
    vmrs r0, fpscr
    str r0, [r8, #8]
    mov r0, #0
    mov r1, #0
    svc #0x0a
    vadd.f32 s2, s0, s1
    vmov r0, s2
    str r0, [r8, #12]
    svc #9
.ltorg
