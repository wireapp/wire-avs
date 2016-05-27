# -- Run test with acm1 + 16 kHz clean channel --
./acm_test -codec opus -fs 16000 -file_path ../../files/ -in far16.pcm -rtp rtp.dat -out acm1_out1.pcm

# -- Run test with acm1 + 16 kHz jittery channel --
./acm_test -codec opus -fs 16000 -file_path ../../files/ -in far16.pcm -rtp rtp.dat -out acm1_out2.pcm -nw_type wifi

# -- Run test with acm1 + 32 kHz clean channel --
./acm_test -codec opus -fs 32000 -file_path ../../files/ -in far32.pcm -rtp rtp.dat -out acm1_out3.pcm

# -- Run test with acm1 + 32 kHz jittery channel --
./acm_test -codec opus -fs 32000 -file_path ../../files/ -in far32.pcm -rtp rtp.dat -out acm1_out4.pcm -nw_type wifi
