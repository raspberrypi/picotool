.cpu cortex-m0
.thumb
  push {r4, lr}
  mov r4, #20
  ldrh r0, [r4]      // r0 = function_table
  ldrh r1, args
  ldrh r4, [r4, #4]  // r4 = table_lookup
  blx r4
  mov r4, r0
  ldr r0, args + 4
  ldr r1, args + 8
  ldr r2, args + 12
  ldrb r3, args + 16
  blx r4
  pop {r4, pc}
.balign 4, 0
args:
