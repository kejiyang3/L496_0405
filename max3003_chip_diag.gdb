# === MAX30003 Chip-Level Validation GDB Batch ===
# Run: arm-none-eabi-gdb -q -batch -x max3003_chip_diag.gdb build/L496_0405.elf

target extended-remote localhost:3333
monitor halt

echo === 1. ALL REGISTERS (via RAM mirror g_max30003_init_value) ===\n
p/x g_max30003_init_value

echo === 2. STATUS register ===\n
p/x g_ecg_rec.last_status
echo STATUS decoded:\n
echo   PLLINT (bit16): check if set = PLL lost lock\n
echo   DCLOFFINT (bit20): lead-off detected\n
echo   EINT (bit23): new sample ready\n

echo === 3. PLL stats ===\n
p/d g_ecg_rec.pll_status_seen_count
p/d g_ecg_rec.pll_edge_count
p/d g_ecg_rec.pll_warn_count

echo === 4. EINT stats ===\n
p/d g_ecg_rec.diag_status_eint_total
p/d g_ecg_rec.diag_eint_hits
p/d g_ecg_rec.diag_status_eovf_total

echo === 5. INTB / EXTI stats ===\n
p/d g_ecg_rec.diag_intb_low_count
p/d g_ecg_rec.diag_intb_high_count

echo === 6. FIFO stats ===\n
p/d g_ecg_rec.fifo_sample_count
p/d g_ecg_rec.fifo_valid_count
p/d g_ecg_rec.fifo_fast_count
p/d g_ecg_rec.fifo_empty_count
p/d g_ecg_rec.fifo_last_count
p/d g_ecg_rec.fifo_eovf_count
p/d g_ecg_rec.fifo_etag_overflow_count
p/d g_ecg_rec.fifo_unknown_etag_count

echo === 7. ETAG histogram ===\n
p/d g_ecg_rec.etag_hist[0]
p/d g_ecg_rec.etag_hist[1]
p/d g_ecg_rec.etag_hist[2]
p/d g_ecg_rec.etag_hist[3]
p/d g_ecg_rec.etag_hist[4]
p/d g_ecg_rec.etag_hist[5]
p/d g_ecg_rec.etag_hist[6]
p/d g_ecg_rec.etag_hist[7]

echo === 8. Task timing ===\n
p/d g_ecg_rec.diag_task_calls
p/d g_ecg_rec.diag_notify_wakes
p/d g_ecg_rec.diag_notify_timeouts
p/d g_ecg_rec.diag_max_gap_ms
p/d g_ecg_rec.diag_max_gap_recording_ms
p/d g_ecg_rec.diag_sensor_task_loop_us_max
p/d g_ecg_rec.diag_max3003_task_us_max
p/d g_ecg_rec.max30003_burst_us_max
p/d g_ecg_rec.max30003_burst_us_count

echo === 9. ECG sample counts ===\n
p/d g_ecg_rec.ecg_sample_count
p/d g_ecg_rec.ecg_written_count

echo === 10. Lead status ===\n
p/d g_ecg_rec.lead_on_count
p/d g_ecg_rec.lead_off_count

echo === DONE ===\n
monitor resume
monitor shutdown
quit
