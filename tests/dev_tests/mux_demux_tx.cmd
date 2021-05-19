--adapter EFA --local_ip 10.0.0.212
--log_component "PROBE ENDPOINT_MANAGER PERFORMANCE_METRICS"
--log_level DEBUG
--stderr

# ------------------------------------------------------------------------------------
# Single TX demux to Rx. Use a single Rx cdi_test listening on one port to test Rx
# mux.
# ------------------------------------------------------------------------------------
-XS --buffer_type LINEAR -name TX1 --stats_period 1 --num_transactions 100 --tx AVM --keep_alive --rate 60
-S --id 1 --remote_ip 10.0.0.214 --dest_port 3100 --payload_size 5184000 --pattern INC --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0
-S --id 2 --remote_ip 10.0.0.214 --dest_port 3100 --payload_size 6216 --pattern INC --avm_audio 51 48kHz EN
-S --id 3 --remote_ip 10.0.0.214 --dest_port 3100 --payload_size 50 --pattern INC --pat_start 1 --avm_anc

# ----------------------------------------------------------------------------
# Single TX demux to multiple Rx's. Use one or more Rx cdi_test's.
# ----------------------------------------------------------------------------
#-XS --buffer_type LINEAR -name TX1 --stats_period 1 --num_transactions 100 --tx AVM --keep_alive --rate 60
#-S --id 1 --remote_ip 10.0.0.214 --dest_port 3100 --payload_size 5184000 --pattern INC --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0
#-S --id 2 --remote_ip 10.0.0.214 --dest_port 3200 --payload_size 6216 --pattern INC --avm_audio 51 48kHz EN
#-S --id 3 --remote_ip 10.0.0.214 --dest_port 3300 --payload_size 50 --pattern INC --pat_start 1 --avm_anc

# ----------------------------------------------------------------------------
# Non-demux test using legacy APIs. Single TX to single Rx. Use a single Rx
# cdi_test listening on single port.
# ----------------------------------------------------------------------------
#-X -name TX1 --remote_ip 10.0.0.214 --dest_port 3100 --num_transactions 100 --tx AVM --keep_alive --rate 60
#-S --id 1 --payload_size 5184000 --pattern INC --avm_video 1920 1080 YCbCr422 Unused 10bit 60 1 BT2020 true false PQ Narrow 16 9 0 1080 0 0
#-S --id 2 --payload_size 6216 --pattern INC --avm_audio 51 48kHz EN
#-S --id 3 --payload_size 50 --pattern SHL --pat_start 1 --avm_anc
