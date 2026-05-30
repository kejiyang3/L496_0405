target extended-remote localhost:3333
monitor halt
set var g_ecg_rec.request_stop = 1
monitor resume
detach
quit
