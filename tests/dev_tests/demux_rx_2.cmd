--local_ip 10.0.0.214 --adapter EFA
--log_component "PROBE ENDPOINT_MANAGER PERFORMANCE_METRICS"
--log_level DEBUG
--stderr

# AVM test Rx Demux stream ID 2.
-X --rx_buffer_delay 0 --stats_period 1 --num_transactions 100 --rx AVM --dest_port 3200 --rate 60
-S --id 2 --payload_size 6216 --pattern INC --avm_audio 51 48kHz EN
