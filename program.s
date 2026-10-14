.section ".text.init"

# Memory-mapped I/O addresses
.equ BUTTON_BASE, 0x70000000
.equ LED_BASE,    0x50000000
.equ GAME_STATE,  0x90000000
.equ SP_BASE,     0x9FFFFFC0

# Register offsets for button controller
.equ BTN_SRC_OFF, 4
.equ BTN_PTM_OFF, 8
.equ BTN_STM_OFF, 12

.globl _start
_start:
    # Set up game value in memory
    li t0, GAME_STATE
    sw zero, 0(t0)     # Initialize game value to 0 (lose)

    li sp, SP_BASE
    
    # Set up interrupt vector
    lui t0, %hi(interrupt_handler)
    addi t0, t0, %lo(interrupt_handler)
    csrw mtvec, t0
    
    li t0, BUTTON_BASE
    
    li t1, 0x55555
    sw t1, BTN_PTM_OFF(t0)

    li t1, 0x5555
    sw t1, BTN_STM_OFF(t0)

    li t0, 0x800
    csrs mie, t0

    li t0, 0x8
    csrs mstatus, t0
    
    li s1, LED_BASE
    
    # Long wait loop
    li t0, 5000  # Adjust this value to change the wait time
wait_loop:
    addi t0, t0, -1
    bnez t0, wait_loop
    
    # Load and check game result
    li t0, GAME_STATE
    lw t1, 0(t0)
    beqz t1, lose
    j win
    
lose:
    li t1, 0xFFFF01FF  # Set red LEDs on for all
    sw t1, 0(s1)
    j end_game
    
win:
    # Turn on green LEDs
    li t1, 0xFFFF02FF  # Set green LEDs on for all
    sw t1, 0(s1)
    
end_game:
    # Stop execution
    ebreak

interrupt_handler:
    addi sp, sp, -16
    sw ra, 12(sp)
    sw s0, 8(sp)
    sw s1, 4(sp)
    sw t0, 0(sp)

    li t0, BUTTON_BASE
    sw x0, BTN_SRC_OFF(t0)

    li   t3, GAME_STATE
    li   t4, 1
    sw   t4, 0(t3)

    lw ra, 12(sp)
    lw s0, 8(sp)
    lw s1, 4(sp)
    lw t0, 0(sp)
    addi sp, sp, 16

    mret